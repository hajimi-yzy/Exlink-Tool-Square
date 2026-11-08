package com.exlink.mobile

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.SystemClock
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.CRC32

data class NearbyDevice(val address: String, val name: String, val rssi: Int)

@SuppressLint("MissingPermission")
class BleClient(private val context: Context, private val scope: CoroutineScope) {
    private val manager = context.getSystemService(BluetoothManager::class.java)
    val devices = mutableStateListOf<NearbyDevice>()
    val status = mutableStateOf("未连接")
    val services = mutableStateOf("")
    val hardware = mutableStateOf("")
    val progress = mutableStateOf(0f)
    val telemetry = mutableStateOf("")
    val logs = mutableStateOf("")
    val powerHistory = mutableStateListOf<Float>()
    val isTransferring = mutableStateOf(false)

    private class LinkLost(message: String, cause: Throwable? = null) : IOException(message, cause)
    private class GattFailure(val code: Int, operation: String) : IOException("$operation 失败：$code")
    private data class GattResult(val code: Int, val bytes: ByteArray = byteArrayOf(), val mtu: Int = 23)
    private class Link(val generation: Long, val address: String) {
        var gatt: BluetoothGatt? = null
        val connected = CompletableDeferred<Unit>()
        val bonded = CompletableDeferred<Unit>()
        val ready = CompletableDeferred<Unit>()
        val operations = GattOperationGate<GattResult>()
        val operationLock = Mutex()
        val statusFrames = Channel<OtaStatus>(32, BufferOverflow.DROP_OLDEST)
        var lastStatus: OtaStatus? = null
        var characteristics = emptyMap<UUID, BluetoothGattCharacteristic>()
        var mtu = 23
        var expectedRestart = false
        var retired = false
    }

    private var link: Link? = null
    private var generation = 0L
    private var pollJob: Job? = null
    private var foreground = true
    private var receiverRegistered = false
    private var recoveryDisabled = false
    private val transferLock = Mutex()
    private var expectedFirmwareVersion: String? = null
    private var awaitingFirmwareRestart = false
    private var scanStop: Job? = null
    private fun uuid(suffix: String) = UUID.fromString("7c9e$suffix-7b3f-4f6d-9e1a-71b4c3a22100")
    private val serviceUuid = uuid("0001")
    private val controlUuid = uuid("0002")
    private val dataUuid = uuid("0003")
    private val statusUuid = uuid("0004")
    private val infoUuid = uuid("0005")
    private val telemetryUuid = uuid("0006")
    private val logsUuid = uuid("0007")
    private val cccdUuid = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    private val scanner get() = manager.adapter?.bluetoothLeScanner

    private fun appendLog(value: String) { logs.value = (logs.value + "\n$value\n").takeLast(16_384) }
    private fun current(connection: Link, g: BluetoothGatt) =
        link === connection && !connection.retired && connection.gatt === g
    private fun requireCurrent(connection: Link) {
        val g = connection.gatt ?: throw LinkLost("蓝牙连接已改变")
        if (!current(connection, g)) throw LinkLost("蓝牙连接已改变")
    }

    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val device = if (Build.VERSION.SDK_INT >= 33)
                intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
            else @Suppress("DEPRECATION") intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
            val connection = link ?: return
            if (device?.address != connection.address) return
            when (intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)) {
                BluetoothDevice.BOND_BONDED -> connection.bonded.complete(Unit)
                BluetoothDevice.BOND_NONE -> retire(connection, LinkLost("配对失败，请在系统蓝牙设置检查配对后重新连接"))
            }
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = NearbyDevice(result.device.address,
                result.scanRecord?.deviceName ?: result.device.name ?: "未命名设备", result.rssi)
            scope.launch(Dispatchers.Main.immediate) {
                val index = devices.indexOfFirst { it.address == device.address }
                if (index < 0) devices.add(device) else devices[index] = device
            }
        }
        override fun onScanFailed(errorCode: Int) {
            scope.launch(Dispatchers.Main.immediate) { status.value = "扫描失败：$errorCode" }
        }
    }

    fun scan() {
        stopScan()
        check(manager.adapter?.isEnabled == true) { "请开启手机蓝牙" }
        devices.clear(); status.value = "扫描中"
        scanner?.startScan(listOf(ScanFilter.Builder().setServiceUuid(android.os.ParcelUuid(serviceUuid)).build()),
            ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCallback)
        scanStop = scope.launch { delay(10_000); stopScan(); if (link == null) status.value = "扫描结束" }
    }

    fun stopScan() { scanStop?.cancel(); runCatching { scanner?.stopScan(scanCallback) } }

    fun connect(address: String) {
        check(!isTransferring.value) { "传输中，请等待完成" }
        recoveryDisabled = false
        stopScan(); pollJob?.cancel()
        openConnection(address)
    }

    private fun registerBondReceiver() {
        if (receiverRegistered) return
        val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= 33) context.registerReceiver(bondReceiver, filter, Context.RECEIVER_EXPORTED)
        else context.registerReceiver(bondReceiver, filter)
        receiverRegistered = true
    }

    private fun openConnection(address: String): Link {
        link?.let { retire(it, LinkLost("连接已更换"), updateStatus = false) }
        registerBondReceiver()
        val connection = Link(++generation, address)
        link = connection
        services.value = ""; hardware.value = ""; telemetry.value = ""; status.value = "连接中"
        try {
            connection.gatt = manager.adapter.getRemoteDevice(address).connectGatt(context, false,
                gattCallback(connection), BluetoothDevice.TRANSPORT_LE)
            check(connection.gatt != null) { "无法创建蓝牙连接" }
        } catch (e: Exception) {
            retire(connection, LinkLost(e.message ?: "无法连接蓝牙", e))
            return connection
        }
        scope.launch(Dispatchers.Main.immediate) {
            try {
                withTimeout(15_000) { connection.connected.await() }
                requireCurrent(connection)
                val g = connection.gatt ?: throw LinkLost("蓝牙已断开")
                if (g.device.bondState != BluetoothDevice.BOND_BONDED) {
                    status.value = "等待安全配对"
                    if (g.device.bondState == BluetoothDevice.BOND_NONE)
                        check(g.device.createBond()) { "无法开始配对，请在系统蓝牙设置检查设备" }
                    withTimeout(45_000) { connection.bonded.await() }
                }
                requireCurrent(connection)
                // The bond broadcast releases this setup coroutine; it never starts
                // discovery alongside an MTU request or another discovery.
                status.value = "协商蓝牙 MTU"
                val negotiated = operation(connection, "MTU", 8_000, tolerateFailure = true) { it.requestMtu(517) }
                if (negotiated.code == BluetoothGatt.GATT_SUCCESS) connection.mtu = negotiated.mtu
                status.value = "发现设备服务"
                operation(connection, "DISCOVER", 10_000) { it.discoverServices() }
                services.value = g.services.joinToString("\n") { it.uuid.toString() }
                val service = g.getService(serviceUuid)
                    ?: throw GattFailure(1, "找不到更新服务，可能是旧 GATT 缓存")
                val required = listOf(controlUuid, dataUuid, statusUuid, infoUuid, telemetryUuid, logsUuid)
                connection.characteristics = required.associateWith { id -> service.getCharacteristic(id)
                    ?: throw GattFailure(1, "设备缺少特征 $id，可能是旧 GATT 缓存") }
                validateCharacteristics(connection)
                // A saved bond does not prove this new link is encrypted. Reading an
                // encrypted characteristic triggers re-encryption and proves it completed.
                status.value = "验证加密连接"
                var secure = false
                repeat(3) { attempt ->
                    if (!secure) {
                        try {
                            val value = read(connection, telemetryUuid, allowSecurityRetry = true).toString(Charsets.UTF_8)
                            if (value.isBlank()) throw GattFailure(5, "设备加密连接尚未就绪")
                            telemetry.value = value
                            secure = true
                        } catch (e: GattFailure) {
                            if (e.code !in setOf(5, 15) || attempt == 2) throw e
                            delay(500)
                        }
                    }
                }
                val characteristic = connection.characteristics.getValue(statusUuid)
                check(g.setCharacteristicNotification(characteristic, true)) { "无法启用更新通知" }
                val descriptor = characteristic.getDescriptor(cccdUuid)
                    ?: throw GattFailure(1, "设备缺少更新通知描述符")
                status.value = "订阅更新状态"
                operation(connection, "DESCRIPTOR:$statusUuid", 8_000) {
                    if (Build.VERSION.SDK_INT >= 33)
                        it.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothStatusCodes.SUCCESS
                    else { descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; it.writeDescriptor(descriptor) }
                }
                hardware.value = read(connection, infoUuid).toString(Charsets.UTF_8)
                val info = org.json.JSONObject(hardware.value)
                check(info.optString("model") == "exlink-co5300-31pin" && info.optInt("protocol") == 1) { "设备型号或升级协议不匹配" }
                requireCurrent(connection)
                status.value = "已就绪 $address（MTU ${connection.mtu}）"
                if (awaitingFirmwareRestart) {
                    val actual = info.optString("version")
                    val expected = expectedFirmwareVersion
                    status.value = when {
                        expected == null -> "设备已重连，固件版本 $actual；请核对目标版本"
                        actual == expected -> "固件升级已确认，版本 $actual"
                        else -> "设备已重连，但固件版本 $actual 与目标 $expected 不符，请检查升级结果"
                    }
                    awaitingFirmwareRestart = false
                    expectedFirmwareVersion = null
                }
                connection.ready.complete(Unit)
                if (!isTransferring.value) startPolling()
            } catch (e: Exception) {
                if (e is CancellationException && connection.retired) return@launch
                if (!connection.retired) retire(connection, LinkLost("连接准备失败：${e.message}", e),
                    refreshCache = e is GattFailure && e.code == 1)
            }
        }
        return connection
    }

    private fun validateCharacteristics(connection: Link) {
        fun requireProperty(id: UUID, mask: Int) {
            if (connection.characteristics.getValue(id).properties and mask == 0)
                throw GattFailure(1, "设备特征属性不兼容 $id")
        }
        requireProperty(controlUuid, BluetoothGattCharacteristic.PROPERTY_WRITE)
        requireProperty(dataUuid, BluetoothGattCharacteristic.PROPERTY_WRITE)
        requireProperty(statusUuid, BluetoothGattCharacteristic.PROPERTY_READ)
        requireProperty(statusUuid, BluetoothGattCharacteristic.PROPERTY_NOTIFY)
        for (id in listOf(infoUuid, telemetryUuid, logsUuid)) requireProperty(id, BluetoothGattCharacteristic.PROPERTY_READ)
    }

    private fun gattCallback(connection: Link) = object : BluetoothGattCallback() {
        private fun deliver(g: BluetoothGatt, action: () -> Unit) {
            scope.launch(Dispatchers.Main.immediate) { if (current(connection, g)) action() }
        }
        override fun onConnectionStateChange(g: BluetoothGatt, code: Int, state: Int) = deliver(g) {
            if (code == BluetoothGatt.GATT_SUCCESS && state == BluetoothProfile.STATE_CONNECTED)
                connection.connected.complete(Unit)
            else if (code != BluetoothGatt.GATT_SUCCESS || state == BluetoothProfile.STATE_DISCONNECTED ||
                state == BluetoothProfile.STATE_DISCONNECTING)
                retire(connection, LinkLost("蓝牙已断开（$code）"), updateStatus = !connection.expectedRestart,
                    refreshCache = code == 1)
        }
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, code: Int) = deliver(g) {
            connection.operations.complete(connection.generation, "MTU", GattResult(code, mtu = mtu))
        }
        override fun onServicesDiscovered(g: BluetoothGatt, code: Int) = deliver(g) {
            connection.operations.complete(connection.generation, "DISCOVER", GattResult(code))
        }
        override fun onDescriptorWrite(g: BluetoothGatt, descriptor: BluetoothGattDescriptor, code: Int) = deliver(g) {
            connection.operations.complete(connection.generation, "DESCRIPTOR:${descriptor.characteristic.uuid}", GattResult(code))
        }
        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray, code: Int) = deliver(g) {
            connection.operations.complete(connection.generation, "READ:${c.uuid}", GattResult(code, value.copyOf()))
        }
        @Deprecated("Legacy Android callback")
        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, code: Int) = deliver(g) {
            connection.operations.complete(connection.generation, "READ:${c.uuid}", GattResult(code, c.value?.copyOf() ?: byteArrayOf()))
        }
        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, code: Int) = deliver(g) {
            connection.operations.complete(connection.generation, "WRITE:${c.uuid}", GattResult(code))
        }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) = deliver(g) {
            receive(connection, c.uuid, value)
        }
        @Deprecated("Legacy Android callback")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) = deliver(g) {
            receive(connection, c.uuid, c.value?.copyOf() ?: return@deliver)
        }
    }

    private fun receive(connection: Link, id: UUID, bytes: ByteArray) {
        if (id != statusUuid) return
        val frame = OtaStatus.parse(bytes) ?: return
        connection.lastStatus = frame
        connection.statusFrames.trySend(frame)
    }

    private fun retire(connection: Link, cause: LinkLost, updateStatus: Boolean = true, refreshCache: Boolean = false) {
        if (connection.retired) return
        connection.retired = true
        connection.connected.completeExceptionally(cause)
        connection.bonded.completeExceptionally(cause)
        connection.ready.completeExceptionally(cause)
        connection.operations.fail(connection.generation, cause)
        connection.statusFrames.close(cause)
        val g = connection.gatt
        if (refreshCache && g != null) {
            // No public Android cache invalidation API exists. Only Invalid Handle or a
            // missing discovered attribute justifies this best-effort hidden API. 133
            // alone receives a fresh connection without refreshing the cache.
            val refreshed = runCatching { g.javaClass.getMethod("refresh").invoke(g) == true }.getOrDefault(false)
            appendLog("GATT 属性失效，缓存刷新${if (refreshed) "已请求" else "不可用"}；请重新连接")
        }
        runCatching { g?.disconnect() }
        runCatching { g?.close() }
        connection.gatt = null
        connection.characteristics = emptyMap()
        if (link === connection) {
            link = null; pollJob?.cancel(); hardware.value = ""; telemetry.value = ""; services.value = ""
            if (updateStatus) status.value = cause.message ?: "蓝牙已断开"
        }
    }

    private suspend fun operation(connection: Link, key: String, timeout: Long, tolerateFailure: Boolean = false,
        start: (BluetoothGatt) -> Boolean): GattResult = withContext(Dispatchers.Main.immediate) {
        connection.operationLock.withLock {
            val g = connection.gatt?.takeIf { current(connection, it) } ?: throw LinkLost("蓝牙已断开")
            val request = connection.operations.begin(connection.generation, key)
            try {
                if (!start(g)) {
                    if (key == "MTU") return@withLock GattResult(-1)
                    throw LinkLost("$key 未被蓝牙系统接受，请重新连接")
                }
                val result = withTimeoutOrNull(timeout) { request.result.await() }
                    ?: throw LinkLost("$key 超时，连接已关闭以丢弃过期回调")
                if (!current(connection, g)) throw LinkLost("$key 完成前连接已改变")
                if (result.code != BluetoothGatt.GATT_SUCCESS) {
                    val failure = GattFailure(result.code, key)
                    if (!tolerateFailure || result.code !in setOf(5, 15)) {
                        if (key != "MTU" || result.code in setOf(1, 133))
                            retire(connection, LinkLost(failure.message ?: "蓝牙操作失败", failure), refreshCache = result.code == 1)
                    }
                    if (key != "MTU") throw failure
                }
                result
            } catch (e: CancellationException) {
                // Native GATT cannot cancel an individual request. Retire its generation
                // so a late callback cannot satisfy a subsequent read/write.
                retire(connection, LinkLost("蓝牙操作已取消，请重新连接", e))
                throw e
            } catch (e: LinkLost) {
                retire(connection, e)
                throw e
            } finally { connection.operations.finish(request) }
        }
    }

    private suspend fun awaitReady(): Link {
        val connection = link ?: throw LinkLost("请先连接设备")
        connection.ready.await()
        if (link !== connection || connection.retired) throw LinkLost("蓝牙连接已改变")
        return connection
    }

    private suspend fun read(connection: Link, id: UUID, allowSecurityRetry: Boolean = false): ByteArray {
        val c = connection.characteristics[id] ?: throw LinkLost("设备缺少读取特征，请重新发现服务")
        return operation(connection, "READ:$id", 5_000, tolerateFailure = allowSecurityRetry) { it.readCharacteristic(c) }.bytes
    }

    private suspend fun write(connection: Link, id: UUID, value: ByteArray) {
        val c = connection.characteristics[id] ?: throw LinkLost("设备缺少写入特征，请重新发现服务")
        if (value.size > connection.mtu - 3) throw OtaRejectedException("连接 MTU 已改变，当前数据包无法安全重发，请重新开始更新")
        operation(connection, "WRITE:$id", 10_000) {
            if (Build.VERSION.SDK_INT >= 33)
                it.writeCharacteristic(c, value, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS
            else { c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT; c.value = value; it.writeCharacteristic(c) }
        }
    }

    private fun startPolling() {
        if (pollJob?.isActive == true) return
        val connection = link ?: return
        if (!foreground || isTransferring.value || !connection.ready.isCompleted || connection.retired) return
        pollJob = scope.launch(Dispatchers.Main.immediate) {
            while (isActive && foreground && !isTransferring.value && link === connection) {
                if (transferLock.tryLock()) {
                    try {
                        telemetry.value = read(connection, telemetryUuid).toString(Charsets.UTF_8)
                        runCatching { org.json.JSONObject(telemetry.value).getDouble("power").toFloat() }.getOrNull()?.let {
                            powerHistory.add(it); if (powerHistory.size > 60) powerHistory.removeAt(0)
                        }
                        if (foreground && !isTransferring.value) {
                            val freshLogs = read(connection, logsUuid).toString(Charsets.UTF_8)
                            if (freshLogs.isNotBlank()) logs.value = (logs.value + freshLogs).takeLast(16_384)
                        }
                    } catch (e: CancellationException) {
                        throw e
                    } catch (e: Exception) {
                        appendLog("状态读取失败：${e.message}")
                        // Never repeat reads against stale handles or a failed 133 link.
                        if (!connection.retired) retire(connection, LinkLost("状态读取失败，请重新连接：${e.message}", e))
                        break
                    } finally { transferLock.unlock() }
                }
                delay(1_000)
            }
        }
    }

    fun refreshHardware() {
        check(!isTransferring.value) { "传输中，请稍后读取" }
        check(link?.ready?.isCompleted == true) { "请先等待设备连接就绪" }
        startPolling()
    }

    private suspend fun ack(connection: Link, expected: OtaExpectation, timeout: Long): OtaStatus? {
        return withTimeoutOrNull(timeout) {
            while (true) {
                val value = connection.statusFrames.receive()
                if (value.error == 1) throw OtaRejectedException("请先在设备端授权蓝牙更新，再重新发送")
                if (!value.belongsTo(expected.session, expected.kind, expected.total)) continue
                if (value.state == 3 || value.error != 0)
                    throw OtaRejectedException("设备拒绝传输，错误 ${value.error}")
                if (expected.confirmed(value)) return@withTimeoutOrNull value
                if (value.accepted > expected.to)
                    throw OtaRejectedException("设备确认进度超出当前数据包，停止传输")
            }
            @Suppress("UNREACHABLE_CODE") null
        }
    }

    private fun clearStatus(connection: Link) {
        while (connection.statusFrames.tryReceive().isSuccess) { }
        connection.lastStatus = null
    }

    private suspend fun readStatus(connection: Link): OtaStatus =
        OtaStatus.parse(read(connection, statusUuid))
            ?: throw OtaRejectedException("设备没有返回有效更新状态，无法安全续传")

    private class TransferRecovery(val address: String, val deviceId: String) {
        val retries = OtaRetryBudget(2)
    }

    private suspend fun recover(recovery: TransferRecovery, expected: OtaExpectation): OtaStatus {
        if (recoveryDisabled) throw OtaRejectedException("连接已手动关闭，更新已停止；请重新连接并授权")
        val deadline = SystemClock.elapsedRealtime() + 55_000
        var lastError: Exception? = null
        while (recovery.retries.take()) {
            if (recoveryDisabled) throw OtaRejectedException("连接已手动关闭，更新已停止；请重新连接并授权")
            val remaining = deadline - SystemClock.elapsedRealtime()
            if (remaining <= 1_000) break
            status.value = "连接中断，正在重新连接（${recovery.retries.used}/2）"
            link?.let { retire(it, LinkLost("准备恢复更新连接"), updateStatus = false) }
            delay(800)
            if (recoveryDisabled) throw OtaRejectedException("连接已手动关闭，更新已停止；请重新连接并授权")
            val fresh = openConnection(recovery.address)
            try {
                val value = withTimeoutOrNull(minOf(25_000, remaining - 800)) {
                    fresh.ready.await()
                    val info = org.json.JSONObject(hardware.value)
                    if (info.optString("id") != recovery.deviceId)
                        throw OtaRejectedException("重新连接的设备身份不一致，停止续传")
                    readStatus(fresh)
                } ?: throw LinkLost("重新连接超时")
                expected.recover(value)
                status.value = "连接已恢复，从 ${value.accepted}/${value.total} 字节继续"
                return value
            } catch (e: CancellationException) {
                throw e
            } catch (e: OtaRejectedException) {
                throw e
            } catch (e: Exception) {
                lastError = e
                retire(fresh, LinkLost("恢复连接失败：${e.message}", e))
            }
        }
        throw LinkLost("更新连接恢复失败，已停止自动重试；请重新授权后开始。${lastError?.message ?: ""}", lastError)
    }

    private suspend fun sendConfirmed(id: UUID, packet: ByteArray, expected: OtaExpectation,
        recovery: TransferRecovery, timeout: Long = 3_000): OtaStatus {
        repeat(3) { attempt ->
            val connection = link
            try {
                val ready = awaitReady()
                clearStatus(ready)
                write(ready, id, packet)
                ack(ready, expected, timeout)?.let { return it }
                // Status is read serially before a retry: missing notification does not
                // mean missing flash data, and the offset must never advance speculatively.
                val snapshot = readStatus(ready)
                if (expected.recover(snapshot)) return snapshot
                status.value = "确认超时，重发当前数据包（${attempt + 1}/3）"
            } catch (e: CancellationException) {
                throw e
            } catch (e: OtaRejectedException) {
                throw e
            } catch (e: Exception) {
                connection?.lastStatus?.takeIf { expected.confirmed(it) }?.let { return it }
                val snapshot = recover(recovery, expected)
                if (expected.recover(snapshot)) return snapshot
            }
        }
        throw OtaRejectedException("设备未确认当前数据包，已停止重发")
    }

    suspend fun transfer(file: File, kind: Int, expectedVersion: String? = null) = withContext(Dispatchers.Main.immediate) {
        // Let an in-flight polling read finish before taking ownership of GATT.
        check(!isTransferring.value) { "已有传输正在进行" }
        isTransferring.value = true
        var locked = false
        var completed = false
        try {
            transferLock.lock(); locked = true
            pollJob?.cancelAndJoin()
            transferLocked(file, kind, expectedVersion)
            completed = true
        } catch (e: CancellationException) {
            link?.let { retire(it, LinkLost("更新已取消，请重新连接")) }
            throw e
        } catch (e: Exception) {
            status.value = e.message ?: "更新失败"
            throw e
        } finally {
            isTransferring.value = false
            if (locked) transferLock.unlock()
            if (kind == 2 || !completed) startPolling()
        }
    }

    suspend fun restoreDefaultAssets() = withContext(Dispatchers.Main.immediate) {
        check(!isTransferring.value) { "已有传输正在进行" }
        isTransferring.value = true
        var locked = false
        try {
            transferLock.lock(); locked = true
            pollJob?.cancelAndJoin()
            val connection = awaitReady()
            clearStatus(connection)
            write(connection, controlUuid, byteArrayOf(7))
            val expected = OtaExpectation(0, 0, 0, 0, 0, 0, 0)
            val confirmed = ack(connection, expected, 5_000) ?: readStatus(connection)
            check(expected.confirmed(confirmed)) { "设备未确认恢复默认素材" }
            status.value = "已恢复默认五组表情"
        } finally {
            isTransferring.value = false
            if (locked) transferLock.unlock()
            startPolling()
        }
    }

    private suspend fun transferLocked(file: File, kind: Int, expectedVersion: String?) {
        require(kind in 1..2) { "不支持的更新类型" }
        val connection = awaitReady()
        val info = org.json.JSONObject(hardware.value)
        require(info.optString("model") == "exlink-co5300-31pin" && info.optInt("protocol") == 1) { "设备型号或升级协议不匹配" }
        if (kind == 1) require(info.optBoolean("ota")) { "设备尚未配置 OTA 分区，需先通过 USB 迁移" }
        check(connection.mtu >= 60) { "蓝牙 MTU 太小，请重新连接" }
        require(file.length() in 1..if (kind == 1) 0x400000 else 0x380000) { "文件超过设备分区限制" }
        val bytes = withContext(Dispatchers.IO) { file.readBytes() }
        val random = java.security.SecureRandom()
        var session: Int
        do { session = random.nextInt() } while (session == 0)
        fun command(op: Int) = ByteBuffer.allocate(5).order(ByteOrder.LITTLE_ENDIAN).put(op.toByte()).putInt(session).array()
        val recovery = TransferRecovery(connection.address, info.getString("id"))
        val begin = ByteBuffer.allocate(42).order(ByteOrder.LITTLE_ENDIAN).put(1).put(kind.toByte()).putInt(session).putInt(bytes.size)
            .put(MessageDigest.getInstance("SHA-256").digest(bytes)).array()
        progress.value = 0f
        status.value = "开始更新，请保持 App 在前台"
        var activating = false
        try {
            sendConfirmed(controlUuid, begin, OtaExpectation(session, kind, bytes.size, 0, 0, 1, 1), recovery, 10_000)
            var offset = 0
            while (offset < bytes.size) {
                val activeMtu = link?.takeIf { !it.retired }?.mtu ?: connection.mtu
                check(activeMtu >= 60) { "恢复连接后的 MTU 太小，无法继续更新" }
                val length = minOf(480, activeMtu - 18, bytes.size - offset)
                val crc = CRC32().apply { update(bytes, offset, length) }.value.toInt()
                val packet = ByteBuffer.allocate(15 + length).order(ByteOrder.LITTLE_ENDIAN).put(2).putInt(session)
                    .putInt(offset).putShort(length.toShort()).putInt(crc).put(bytes, offset, length).array()
                sendConfirmed(dataUuid, packet, OtaExpectation(session, kind, bytes.size, offset, offset + length, 1, 1), recovery)
                offset += length
                progress.value = offset.toFloat() / bytes.size
                status.value = "正在传输 ${offset * 100L / bytes.size}%"
            }
            status.value = "正在校验更新文件"
            val end = OtaExpectation(session, kind, bytes.size, bytes.size, bytes.size, 1, 2)
            sendConfirmed(controlUuid, command(3), end, recovery, 15_000)
            if (link == null) recover(recovery, end)
            activating = true
            status.value = "正在激活更新"
            activate(command(4), OtaExpectation(session, kind, bytes.size, bytes.size, bytes.size, 2, 0), kind)
            if (kind == 1) {
                expectedFirmwareVersion = expectedVersion
                awaitingFirmwareRestart = true
                status.value = "升级命令已确认，设备正在重启；重连后核对固件版本"
            } else status.value = "素材更新已确认并应用"
        } catch (e: Exception) {
            // ACTIVATE is irreversible and never retransmitted. Missing confirmation
            // remains uncertain; leave the device for manual version inspection.
            if (!activating && e !is CancellationException) {
                link?.takeIf { !it.retired && it.ready.isCompleted }?.let { live ->
                    runCatching {
                        val snapshot = readStatus(live)
                        if (snapshot.belongsTo(session, kind, bytes.size) && snapshot.state in 1..2)
                            write(live, controlUuid, command(5))
                    }
                }
            }
            throw e
        }
    }

    private suspend fun activate(packet: ByteArray, expected: OtaExpectation, kind: Int) {
        val connection = awaitReady()
        clearStatus(connection)
        var confirmed = false
        try {
            write(connection, controlUuid, packet)
            confirmed = ack(connection, expected, 5_000) != null
            if (!confirmed && !connection.retired) confirmed = expected.confirmed(readStatus(connection))
        } catch (e: CancellationException) {
            throw e
        } catch (e: OtaRejectedException) {
            throw e
        } catch (_: Exception) {
            confirmed = connection.lastStatus?.let { expected.confirmed(it) } == true
        }
        if (!confirmed) throw OtaRejectedException("激活结果未确认，请重连核对固件/素材版本；不要重复发送激活命令")
        if (kind == 1) connection.expectedRestart = true
    }

    fun setAppForeground(value: Boolean) {
        foreground = value
        // Finish an existing poll read, then stop polling without cancelling GATT.
        // Pairing/file-picker dialogs must not close an active update link.
        if (value && !isTransferring.value) startPolling()
    }

    fun close() {
        recoveryDisabled = true
        stopScan(); pollJob?.cancel()
        link?.let { retire(it, LinkLost("已断开")) }
        status.value = "已断开"
        if (receiverRegistered) { context.unregisterReceiver(bondReceiver); receiverRegistered = false }
    }
}

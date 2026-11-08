package com.exlink.mobile

import android.Manifest
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.util.Patterns
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.io.File

private enum class AuthMode { LOGIN, REGISTER }

class MainActivity : ComponentActivity() {
    private lateinit var ble: BleClient
    private val cloud = CloudApi()
    private var authJob: Job? = null
    private val permission = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { result ->
        if (result.values.all { it }) runCatching { ble.scan() }.onFailure { ble.status.value = it.message ?: "扫描失败" }
        else ble.status.value = "需要蓝牙权限才能搜索设备"
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ble = BleClient(this, lifecycleScope)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme(primary = Color(0xFFDFE6EB), background = Color(0xFF111315), surface = Color(0xFF1B1E20))) {
                Surface(Modifier.fillMaxSize()) {
                    var account by remember { mutableStateOf("") }
                    var email by remember { mutableStateOf("") }
                    var password by remember { mutableStateOf("") }
                    var code by remember { mutableStateOf("") }
                    var authMode by remember { mutableStateOf(AuthMode.LOGIN) }
                    var authBusy by remember { mutableStateOf(false) }
                    var codeBusy by remember { mutableStateOf(false) }
                    var codeSentForEmail by remember { mutableStateOf("") }
                    var codeExpiresIn by remember { mutableLongStateOf(300L) }
                    var cooldownDeadline by remember { mutableLongStateOf(0L) }
                    var cooldownSeconds by remember { mutableIntStateOf(0) }
                    var authenticated by remember { mutableStateOf(false) }
                    var localMode by remember { mutableStateOf(false) }
                    var otaAuthorized by remember { mutableStateOf<Boolean?>(null) }
                    var otaCode by remember { mutableStateOf("") }
                    var page by remember { mutableIntStateOf(0) }
                    var message by remember { mutableStateOf("") }
                    var busy by remember { mutableStateOf(false) }
                    var firmware by remember { mutableStateOf(emptyList<Firmware>()) }
                    var downloaded by remember { mutableStateOf<File?>(null) }
                    var downloadedVersion by remember { mutableStateOf<String?>(null) }
                    val lifecycleOwner = LocalLifecycleOwner.current
                    var lifecycleActive by remember { mutableStateOf(lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) }
                    DisposableEffect(lifecycleOwner) {
                        val observer = LifecycleEventObserver { _, _ ->
                            lifecycleActive = lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)
                        }
                        lifecycleOwner.lifecycle.addObserver(observer)
                        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
                    }
                    LaunchedEffect(ble.isTransferring.value) {
                        if (ble.isTransferring.value) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    }
                    DisposableEffect(Unit) {
                        onDispose { window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
                    }
                    LaunchedEffect(cooldownDeadline, lifecycleActive) {
                        if (!lifecycleActive) return@LaunchedEffect
                        while (true) {
                            val left = cooldownDeadline - SystemClock.elapsedRealtime()
                            cooldownSeconds = if (left > 0) ((left + 999L) / 1000L).toInt() else 0
                            if (left <= 0) break
                            delay(minOf(1_000L, left))
                        }
                    }
                    fun clearOtaFiles() {
                        downloaded?.delete()
                        downloaded = null
                        downloadedVersion = null
                        firmware = emptyList()
                        File(cacheDir, "firmware.bin").delete()
                    }
                    fun work(block: suspend () -> Unit) {
                        if (busy || ble.isTransferring.value) return
                        busy = true
                        lifecycleScope.launch {
                            try {
                                block()
                            } catch (e: CancellationException) {
                                throw e
                            } catch (e: Exception) {
                                if (e is CloudApiException && e.invalidatesOta) {
                                    otaAuthorized = null
                                    clearOtaFiles()
                                    if (e.httpStatus == 401) {
                                        cloud.token = ""
                                        authenticated = false
                                    }
                                }
                                message = e.message ?: "操作失败"
                            } finally {
                                busy = false
                            }
                        }
                    }
                    suspend fun refreshOtaAccess() {
                        otaAuthorized = null
                        val allowed = cloud.otaEntitlement()
                        otaAuthorized = allowed
                        if (!allowed) clearOtaFiles()
                    }
                    LaunchedEffect(page, authenticated) {
                        if (page == 1 && authenticated && cloud.isConfigured) work { refreshOtaAccess() }
                    }
                    val requestBusy = authBusy || codeBusy
                    val normalizedEmail = email.trim()
                    val validEmail = normalizedEmail.length <= 254 && Patterns.EMAIL_ADDRESS.matcher(normalizedEmail).matches()
                    fun requestCode() {
                        if (authJob?.isActive == true || !validEmail || cooldownDeadline > SystemClock.elapsedRealtime()) return
                        val submittedEmail = normalizedEmail
                        authJob = lifecycleScope.launch {
                            codeBusy = true
                            message = ""
                            try {
                                val result = cloud.requestEmailCode(submittedEmail)
                                if (authMode == AuthMode.REGISTER && email.trim() == submittedEmail) {
                                    codeSentForEmail = submittedEmail
                                    codeExpiresIn = result.expiresInSeconds
                                    cooldownDeadline = SystemClock.elapsedRealtime() + result.retryAfterSeconds * 1_000L
                                    message = "验证码已发送，请查收邮箱"
                                }
                            } catch (e: CancellationException) {
                                throw e
                            } catch (e: Exception) {
                                message = e.message ?: "验证码发送失败"
                            } finally {
                                codeBusy = false
                            }
                        }
                    }
                    fun submitAuth() {
                        if (authJob?.isActive == true) return
                        val submittedMode = authMode
                        val submittedAccount = if (submittedMode == AuthMode.REGISTER) normalizedEmail else account
                        val submittedPassword = password
                        val submittedCode = code
                        authJob = lifecycleScope.launch {
                            authBusy = true
                            message = ""
                            try {
                                val candidateToken = if (submittedMode == AuthMode.REGISTER) {
                                    cloud.register(submittedAccount, submittedPassword, submittedCode)
                                } else {
                                    cloud.login(submittedAccount, submittedPassword)
                                }
                                val stillMatches = authMode == submittedMode && password == submittedPassword && if (submittedMode == AuthMode.REGISTER) {
                                        email.trim() == submittedAccount && code == submittedCode
                                    } else account == submittedAccount
                                if (stillMatches) {
                                    cloud.token = candidateToken
                                    authenticated = true
                                    localMode = false
                                    otaAuthorized = null
                                    clearOtaFiles()
                                    password = ""
                                    code = ""
                                    message = if (submittedMode == AuthMode.REGISTER) "注册并登录成功" else "已登录"
                                }
                            } catch (e: CancellationException) {
                                throw e
                            } catch (e: Exception) {
                                message = e.message ?: "账号请求失败"
                            } finally {
                                authBusy = false
                            }
                        }
                    }
                    Column(Modifier.fillMaxSize().padding(WindowInsets.safeDrawing.asPaddingValues()).padding(20.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        Text("EXLINK / CONTROL", style = MaterialTheme.typography.headlineSmall)
                        Text("设备 · 固件 · 息屏素材", color = Color.Gray)
                        if (!cloud.isConfigured) Text("此版本未配置云服务，本地设备与息屏素材功能可用。", style = MaterialTheme.typography.bodySmall)
                        if (!authenticated && !localMode) {
                            if (!cloud.isConfigured) {
                                Button(onClick = { localMode = true }, modifier = Modifier.fillMaxWidth()) { Text("使用本地设备功能") }
                            }
                            if (cloud.isConfigured) {
                            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                if (authMode == AuthMode.LOGIN) Button(enabled = !requestBusy, onClick = { authMode = AuthMode.LOGIN }, modifier = Modifier.weight(1f)) { Text("登录") }
                                else OutlinedButton(enabled = !requestBusy, onClick = { authMode = AuthMode.LOGIN }, modifier = Modifier.weight(1f)) { Text("登录") }
                                if (authMode == AuthMode.REGISTER) Button(enabled = !requestBusy, onClick = { authMode = AuthMode.REGISTER }, modifier = Modifier.weight(1f)) { Text("注册") }
                                else OutlinedButton(enabled = !requestBusy, onClick = { authMode = AuthMode.REGISTER; message = "" }, modifier = Modifier.weight(1f)) { Text("注册") }
                            }
                            if (authMode == AuthMode.LOGIN) {
                                OutlinedTextField(account, { account = it }, enabled = !requestBusy, label = { Text("账号") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                            } else {
                                OutlinedTextField(email, {
                                    email = it.take(254)
                                    code = ""
                                    codeSentForEmail = ""
                                }, enabled = !requestBusy, label = { Text("邮箱") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email), singleLine = true, modifier = Modifier.fillMaxWidth())
                            }
                            OutlinedTextField(password, { password = it }, enabled = !requestBusy, label = { Text("密码") }, visualTransformation = PasswordVisualTransformation(), singleLine = true, modifier = Modifier.fillMaxWidth())
                            if (authMode == AuthMode.REGISTER) {
                                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    OutlinedTextField(code, { code = it.filter { character -> character in '0'..'9' }.take(6) }, enabled = !requestBusy, label = { Text("6 位验证码") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), singleLine = true, modifier = Modifier.weight(1f))
                                    OutlinedButton(enabled = !requestBusy && validEmail && cooldownSeconds == 0, onClick = { requestCode() }) {
                                        Text(if (cooldownSeconds > 0) "${cooldownSeconds}s" else "获取验证码")
                                    }
                                }
                                Text("验证码 ${codeExpiresIn / 60} 分钟内有效；密码须为 8 至 128 个字符。", style = MaterialTheme.typography.bodySmall)
                                if (codeSentForEmail == normalizedEmail) Text("验证码已发送至当前邮箱。", style = MaterialTheme.typography.bodySmall)
                            }
                            val canSubmit = !requestBusy && if (authMode == AuthMode.LOGIN) account.isNotBlank() && password.isNotBlank()
                                else validEmail && password.length in 8..128 && code.length == 6
                            Button(enabled = canSubmit, onClick = { submitAuth() }, modifier = Modifier.fillMaxWidth()) {
                                Text(if (authMode == AuthMode.LOGIN) "登录" else "注册并登录")
                            }
                            }
                        } else {
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                listOf("设备", "OTA", "息屏素材").forEachIndexed { i, label ->
                                    OutlinedButton(enabled = !busy && !ble.isTransferring.value, onClick = { page = i }) { Text(label) }
                                }
                            }
                            when (page) {
                                0 -> {
                                    Text(ble.status.value)
                                    Text("先在设备设置开启 Phone connection。后台暂停状态读取；升级请保持 App 在前台。", style = MaterialTheme.typography.bodySmall)
                                    Button(enabled = !ble.isTransferring.value, onClick = {
                                        val permissions = if (Build.VERSION.SDK_INT >= 31) arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT) else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)
                                        permission.launch(permissions)
                                    }) { Text("搜索设备") }
                                    ble.devices.forEach { device ->
                                        OutlinedButton(enabled = !ble.isTransferring.value, modifier = Modifier.fillMaxWidth(), onClick = { runCatching { ble.connect(device.address) }.onFailure { message = it.message ?: "连接失败" } }) {
                                            Text("${device.name} / ${device.rssi} dBm\n${device.address}")
                                        }
                                    }
                                    if (ble.services.value.isNotBlank()) Text("GATT 服务\n${ble.services.value}", style = MaterialTheme.typography.bodySmall)
                                    OutlinedButton(enabled = !ble.isTransferring.value, onClick = { runCatching { ble.refreshHardware() }.onFailure { message = it.message ?: "读取失败" } }) { Text("读取设备状态") }
                                    if (ble.hardware.value.isNotBlank()) Text(ble.hardware.value)
                                    HardwarePanel(ble)
                                }
                                1 -> {
                                    Text("升级前关闭电源输出，保持设备供电和蓝牙连接。")
                                    Text("升级状态：${ble.status.value}")
                                    if (authenticated && cloud.isConfigured) {
                                    Text(when (otaAuthorized) {
                                        true -> "OTA 服务：已授权"
                                        false -> "OTA 服务：未授权"
                                        null -> "OTA 服务：待确认"
                                    })
                                    OutlinedButton(enabled = !busy && !ble.isTransferring.value, onClick = { work { refreshOtaAccess() } }) { Text("刷新授权状态") }
                                    if (otaAuthorized != true) {
                                        OutlinedTextField(otaCode, { otaCode = it.take(128) }, enabled = !busy && !ble.isTransferring.value,
                                            label = { Text("OTA 授权码") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                                        Button(enabled = !busy && !ble.isTransferring.value && otaCode.isNotBlank(), onClick = { work {
                                            otaAuthorized = null
                                            try {
                                                val granted = cloud.redeemOta(otaCode)
                                                otaAuthorized = granted
                                                if (granted) {
                                                    otaCode = ""
                                                    message = "已绑定当前账号，可使用 OTA 服务"
                                                } else clearOtaFiles()
                                            } catch (e: CloudApiException) {
                                                if (e.errorCode != "ota_already_authorized") throw e
                                                refreshOtaAccess()
                                                message = "已刷新当前账号的 OTA 授权"
                                            }
                                        } }) { Text("兑换授权码") }
                                        Text("每个授权码只能兑换一次，兑换后绑定当前账号长期使用。", style = MaterialTheme.typography.bodySmall)
                                    }
                                    Button(enabled = !busy && !ble.isTransferring.value && otaAuthorized == true, onClick = { work {
                                        firmware = cloud.firmwareList()
                                        message = "获取 ${firmware.size} 个版本"
                                    } }) { Text("获取云端固件") }
                                    firmware.forEach { item ->
                                        OutlinedButton(enabled = !busy && !ble.isTransferring.value && otaAuthorized == true, modifier = Modifier.fillMaxWidth(), onClick = { work {
                                            downloaded?.delete()
                                            downloaded = null
                                            downloadedVersion = null
                                            downloaded = cloud.download(item, File(cacheDir, "firmware.bin"))
                                            downloadedVersion = item.version
                                            message = "${item.version} 下载及 SHA-256 校验完成"
                                        } }) {
                                            Text("${item.model} / ${item.version} / ${item.size / 1024} KiB")
                                        }
                                    }
                                    if (downloaded != null) Button(enabled = !busy && !ble.isTransferring.value && otaAuthorized == true, onClick = { message = ""; work {
                                        refreshOtaAccess()
                                        if (otaAuthorized != true) throw CloudApiException(403, "ota_not_authorized")
                                        ble.transfer(downloaded!!, 1, expectedVersion = downloadedVersion)
                                    } }) { Text("通过蓝牙升级") }
                                    } else Text("云端 OTA 不可用，可继续使用设备与息屏素材功能。")
                                    LinearProgressIndicator(progress = { ble.progress.value }, modifier = Modifier.fillMaxWidth())
                                }
                                2 -> AssetEditor(onRestore = { ble.restoreDefaultAssets() }) { file -> ble.transfer(file, 2) }
                            }
                        }
                        if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
                        if (message.isNotBlank()) Text(message)
                    }
                }
            }
        }
    }
    override fun onStart() {
        super.onStart()
        ble.setAppForeground(true)
    }
    override fun onStop() {
        // Background telemetry polling would continually reset the device's
        // inactivity timer. Pause polling; incomplete transfers abort safely.
        ble.setAppForeground(false)
        authJob?.cancel()
        super.onStop()
    }
    override fun onDestroy() { ble.close(); super.onDestroy() }
}

package com.exlink.mobile

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.util.Locale

@Composable
fun AssetEditor(onRestore: suspend () -> Unit, onTransfer: suspend (File) -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var source by remember { mutableStateOf<Bitmap?>(null) }
    var preview by remember { mutableStateOf<Bitmap?>(null) }
    var gif by remember { mutableStateOf<ByteArray?>(null) }
    var sourceBytes by remember { mutableIntStateOf(0) }
    var converted by remember { mutableStateOf<AssetEncoder.Result?>(null) }
    var conversionJob by remember { mutableStateOf<Job?>(null) }
    var busy by remember { mutableStateOf(false) }
    var horizontal by remember { mutableFloatStateOf(0.5f) }
    var vertical by remember { mutableFloatStateOf(0.5f) }
    var message by remember { mutableStateOf("裁剪为 480 × 480。GIF 最长 25 秒，优先 10 fps，超出设备容量时自动减少采样帧。转换后确认再上传；素材只在手机处理。") }
    val importer = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) scope.launch {
            busy = true
            try {
                val imported = withContext(Dispatchers.IO) {
                    context.contentResolver.openInputStream(uri)?.use { input ->
                        input.readBytesLimited(16 * 1024 * 1024)
                    } ?: error("无法读取文件")
                }
                val gifBytes = imported.takeIf { it.size >= 3 && it[0] == 71.toByte() && it[1] == 73.toByte() && it[2] == 70.toByte() }
                val bitmap = withContext(Dispatchers.IO) {
                    val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, options) }
                    require(options.outWidth > 0) { "无法读取图片" }
                    val sample = generateSequence(1) { it * 2 }.first { options.outWidth / it <= 1920 && options.outHeight / it <= 1920 }
                    context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, BitmapFactory.Options().apply { inSampleSize = sample }) }
                }
                source = bitmap; gif = gifBytes; sourceBytes = imported.size; converted = null
                message = "${if (gifBytes != null) "GIF" else "图片"} 已导入，源文件 ${imported.size / 1024} KiB。设备容量按转换后的 EXA1 大小计算，与源 GIF 大小不同。"
            } catch (e: CancellationException) { throw e }
            catch (e: Exception) { message = e.message ?: "导入失败" }
            finally { busy = false }
        }
    }
    LaunchedEffect(source, horizontal, vertical) {
        source?.let { bitmap ->
            preview = withContext(Dispatchers.Default) {
                AssetEncoder.crop(bitmap, horizontal, vertical)
            }
        }
    }
    Button(enabled = !busy, onClick = { importer.launch("image/*") }) { Text("导入图片 / GIF") }
    OutlinedButton(enabled = !busy, onClick = {
        scope.launch {
            busy = true
            try { onRestore(); message = "已恢复默认五组表情" }
            catch (e: Exception) { message = e.message ?: "恢复失败" }
            finally { busy = false }
        }
    }) { Text("恢复设备默认表情") }
    preview?.let {
        Image(it.asImageBitmap(), contentDescription = "息屏图片裁剪预览", modifier = Modifier.fillMaxWidth().height(240.dp))
        Text("水平裁剪位置"); Slider(horizontal, { value -> horizontal = value; converted = null }, enabled = !busy)
        Text("垂直裁剪位置"); Slider(vertical, { value -> vertical = value; converted = null }, enabled = !busy)
        Button(enabled = !busy, onClick = {
            conversionJob = scope.launch {
                busy = true
                try {
                    converted = null
                    message = "正在转换，源文件 ${sourceBytes / 1024} KiB；将自动适配设备 3.5 MiB 素材槽"
                    val result = withContext(Dispatchers.Default) { AssetEncoder.create(source, gif, horizontal, vertical, File(context.cacheDir, "screensaver.exa")) }
                    converted = result
                    val fps = String.format(Locale.ROOT, "%.1f", result.samples * 1000.0 / result.durationMs)
                    message = "转换后 ${result.file.length() / 1024} KiB / 3584 KiB，${result.frames} 帧，${result.durationMs / 1000.0} 秒。" +
                        (if (gif != null) "采样 $fps fps。" else "") +
                        (if (result.samplingReduced) "为适配容量已减少采样帧，时长保持不变；动画会更不连贯。可使用更短或细节更少的 GIF 提高流畅度。" else "") +
                        (if (gif != null && result.frames == 1) "转换结果仅 1 帧，设备将按静态照片展示，时长由设备 Photo time 设置决定。" else "") +
                        (if (result.trimmed) "源 GIF 超过 25 秒，仅保留前 25 秒。" else "") + "确认后再上传。"
                } catch (e: CancellationException) { message = "转换已取消"; throw e }
                catch (e: Exception) { message = e.message ?: "素材转换失败" }
                finally { busy = false; conversionJob = null }
            }
        }) { Text("转换素材") }
        if (conversionJob != null) OutlinedButton(onClick = { conversionJob?.cancel() }) { Text("取消转换") }
        converted?.let { result ->
            Button(enabled = !busy, onClick = {
                scope.launch {
                    busy = true
                    try { onTransfer(result.file); message = "息屏素材更新完成" }
                    catch (e: CancellationException) { throw e }
                    catch (e: Exception) { message = e.message ?: "素材更新失败" }
                    finally { busy = false }
                }
            }) { Text("确认更新到设备") }
        }
    }
    if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
    Text(message)
}

private fun java.io.InputStream.readBytesLimited(maximum: Int): ByteArray {
    val output = java.io.ByteArrayOutputStream()
    val buffer = ByteArray(8192)
    while (true) { val n = read(buffer); if (n < 0) break; require(output.size() + n <= maximum) { "导入文件超过 16 MiB" }; output.write(buffer, 0, n) }
    return output.toByteArray()
}

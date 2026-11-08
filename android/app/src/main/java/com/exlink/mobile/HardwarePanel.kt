package com.exlink.mobile

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import org.json.JSONObject

@Composable
fun HardwarePanel(ble: BleClient) {
    val json = runCatching { JSONObject(ble.telemetry.value) }.getOrNull()
    if (json != null) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            listOf("voltage" to "V", "current" to "A", "power" to "W").forEach { (key, unit) ->
                Column { Text(unit, color = Color.Gray); Text("%.3f".format(json.optDouble(key, 0.0)), style = MaterialTheme.typography.titleLarge) }
            }
        }
        Text("输出 ${if (json.optBoolean("output")) "开启" else "关闭"} · PWM ${json.opt("pwm")} · 舵机 ${json.opt("servo")}")
        if (!json.isNull("battery")) Text("电量 ${json.opt("battery")}")
    }
    Text("功率 / 最近 60 次采样", style = MaterialTheme.typography.bodySmall)
    val history = ble.powerHistory.toList()
    Canvas(Modifier.fillMaxWidth().height(100.dp)) {
        drawLine(Color.DarkGray, Offset(0f, size.height - 1), Offset(size.width, size.height - 1))
        if (history.size >= 2) {
            val max = history.maxOrNull()?.coerceAtLeast(1f) ?: 1f
            for (i in 1 until history.size) {
                fun point(index: Int) = Offset(index.toFloat() / (history.size - 1) * size.width, size.height - history[index].coerceAtLeast(0f) / max * size.height)
                drawLine(Color(0xFFDFE6EB), point(i - 1), point(i), 2.dp.toPx())
            }
        }
    }
    if (ble.logs.value.isNotEmpty()) Text("设备日志\n${ble.logs.value}", style = MaterialTheme.typography.bodySmall)
}

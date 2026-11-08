package com.exlink.mobile

import org.json.JSONObject
import java.net.URI

/** An unconfigured public build has no cloud endpoint and never falls back to another server. */
data class CloudSettings(val origin: String, val fingerprint: String) {
    val configured: Boolean get() = origin.isNotEmpty()

    companion object {
        fun parse(origin: String, fingerprint: String): CloudSettings {
            val base = origin.trim().trimEnd('/')
            val pin = fingerprint.replace(Regex("[:\\s]"), "").uppercase()
            if (base.isEmpty() && pin.isEmpty()) return CloudSettings("", "")
            val uri = runCatching { URI(base) }.getOrNull()
            val ipv4 = uri?.host.orEmpty().split('.').let { parts ->
                parts.size == 4 && parts.all { part ->
                    part.isNotEmpty() && part.all(Char::isDigit) && part.toIntOrNull()?.let { it in 0..255 } == true
                }
            }
            require(uri != null && uri.scheme == "https" && ipv4 && uri.userInfo == null &&
                uri.query == null && uri.fragment == null && (uri.path.isEmpty() || uri.path == "/") &&
                (uri.port == -1 || uri.port in 1..65535)) { "内置服务器配置无效" }
            require(pin.matches(Regex("[0-9A-F]{64}"))) { "内置证书配置无效" }
            return CloudSettings(base, pin)
        }
    }
}

class CloudApiException(val httpStatus: Int, val errorCode: String?) : IllegalStateException(
    when (errorCode) {
        "invalid_email" -> "邮箱格式无效"
        "mail_not_configured" -> "服务器尚未配置邮件服务"
        "mail_delivery_failed" -> "验证码邮件发送失败，请稍后重试"
        "rate_limited" -> "请求过于频繁，请稍后重试"
        "code_invalid" -> "验证码错误"
        "code_expired" -> "验证码已过期，请重新获取"
        "code_required" -> "请输入邮箱验证码"
        "code_attempts_exceeded" -> "验证码尝试次数过多，请重新获取"
        "account_exists" -> "该邮箱已注册，请直接登录"
        "invalid_credentials" -> "账号或密码错误"
        "invalid_credentials_format", "invalid_password" -> "账号或密码格式不正确"
        "unauthorized" -> "登录已失效，请重新登录"
        "ota_not_authorized" -> "该账号尚未获得 OTA 授权，请先兑换授权码"
        "ota_code_invalid" -> "授权码无效、已使用或已撤销"
        "ota_already_authorized" -> "该账号已获得 OTA 授权，请刷新状态"
        else -> "云端请求失败（HTTP $httpStatus）"
    }
) {
    val invalidatesOta: Boolean get() = httpStatus == 401 || httpStatus == 403
}

fun parseOtaEntitlement(json: JSONObject): Boolean {
    // Missing values and strings such as "true" are not authorization.
    val value = json.opt("ota_authorized")
    check(value is Boolean) { "OTA 授权状态无效，请刷新重试" }
    return value
}

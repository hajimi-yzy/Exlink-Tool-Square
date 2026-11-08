package com.exlink.mobile

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import okhttp3.Call
import okhttp3.Callback
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.URI
import java.security.MessageDigest
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import kotlin.coroutines.resumeWithException
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLException
import javax.net.ssl.X509TrustManager

data class Firmware(val version: String, val model: String, val url: String, val sha256: String, val size: Long)
object ApiConfig {
    const val emailCode = "/api/auth/email-code"
    const val register = "/api/auth/register"
    const val login = "/api/auth/login"
    const val firmware = "/api/firmware?product=exlink-co5300-31pin"
    const val entitlement = "/api/ota/entitlement"
    const val redeem = "/api/ota/redeem"
}

/** API contract is documented in android/README.md; the endpoint and pin are fixed at build time. */
class CloudApi(
    baseUrl: String = BuildConfig.EXLINK_API_BASE_URL,
    certificateFingerprint: String = BuildConfig.EXLINK_TLS_FINGERPRINT_SHA256
) {
    private val settings = CloudSettings.parse(baseUrl, certificateFingerprint)
    val isConfigured: Boolean get() = settings.configured
    private val baseUrl = settings.origin
    private val certificateFingerprint = settings.fingerprint
    private val defaultHttp = OkHttpClient.Builder().followRedirects(false).followSslRedirects(false).build()
    private val http: OkHttpClient by lazy {
        val pin = certificateFingerprint.lowercase()
        val trust = object : X509TrustManager {
            override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
            override fun checkClientTrusted(chain: Array<X509Certificate>?, authType: String?) { throw CertificateException("Client certificates not supported") }
            override fun checkServerTrusted(chain: Array<X509Certificate>?, authType: String?) {
                val leaf = chain?.firstOrNull() ?: throw CertificateException("Missing server certificate")
                leaf.checkValidity()
                val actual = MessageDigest.getInstance("SHA-256").digest(leaf.encoded).joinToString("") { "%02x".format(it) }
                if (actual != pin) throw CertificateException("服务器证书指纹不匹配")
            }
        }
        val ssl = SSLContext.getInstance("TLS").apply { init(null, arrayOf(trust), null) }
        defaultHttp.newBuilder().sslSocketFactory(ssl.socketFactory, trust).build()
    }
    var token = ""
    private fun endpoint(path: String): String {
        check(isConfigured) { "此版本未配置云服务，可使用本地设备和息屏素材功能" }
        return baseUrl.trimEnd('/') + path
    }
    private suspend fun execute(request: Request): Response = suspendCancellableCoroutine { continuation ->
        val call = http.newCall(request)
        continuation.invokeOnCancellation { call.cancel() }
        call.enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                val certificateFailure = generateSequence<Throwable>(e) { it.cause }.any { it is SSLException }
                val message = if (certificateFailure) "HTTPS 证书校验失败，请联系维护者"
                    else "网络请求失败，请稍后重试"
                if (continuation.isActive) continuation.resumeWithException(IOException(message))
            }

            override fun onResponse(call: Call, response: Response) {
                continuation.resume(response) { _, cancelledResponse, _ -> cancelledResponse.close() }
            }
        })
    }

    private fun apiError(response: Response): Nothing {
        val error = runCatching {
            response.body?.string()?.let(::JSONObject)?.optString("error")
        }.getOrNull()
        throw CloudApiException(response.code, error)
    }

    private suspend fun post(path: String, body: JSONObject, authorized: Boolean = false): JSONObject {
        val request = Request.Builder().url(endpoint(path))
            .post(body.toString().toRequestBody("application/json".toMediaType()))
        if (authorized) {
            check(token.isNotBlank()) { "请先注册或登录" }
            request.header("Authorization", "Bearer $token")
        }
        return execute(request.build()).use { response ->
            if (!response.isSuccessful) apiError(response)
            runCatching { JSONObject(response.body?.string().orEmpty()) }
                .getOrElse { throw IllegalStateException("账号服务返回的数据无效") }
        }
    }

    suspend fun requestEmailCode(email: String): EmailCodeResult {
        val json = post(ApiConfig.emailCode, JSONObject().put("email", email))
        if (json.optString("status") != "sent") throw IllegalStateException("验证码发送状态无效")
        return EmailCodeResult(
            retryAfterSeconds = json.optLong("retry_after", 60L).coerceAtLeast(1L),
            expiresInSeconds = json.optLong("expires_in", 300L).coerceAtLeast(1L)
        )
    }

    suspend fun login(account: String, password: String): String {
        val body = JSONObject().put("account", account).put("password", password).toString()
        return post(ApiConfig.login, JSONObject(body)).optString("access_token")
            .takeIf { it.isNotBlank() } ?: throw IllegalStateException("账号服务未返回登录凭据")
    }

    suspend fun register(email: String, password: String, code: String): String {
        val body = JSONObject().put("account", email).put("password", password).put("code", code)
        return post(ApiConfig.register, body).optString("access_token")
            .takeIf { it.isNotBlank() } ?: throw IllegalStateException("账号服务未返回登录凭据")
    }
    suspend fun otaEntitlement(): Boolean {
        check(token.isNotBlank()) { "请先注册或登录" }
        val request = Request.Builder().url(endpoint(ApiConfig.entitlement))
            .header("Authorization", "Bearer $token").build()
        return execute(request).use { response ->
            if (!response.isSuccessful) apiError(response)
            parseOtaEntitlement(JSONObject(response.body?.string().orEmpty()))
        }
    }

    suspend fun redeemOta(code: String): Boolean {
        val normalized = code.trim()
        require(normalized.isNotEmpty() && normalized.length <= 128) { "请输入有效授权码" }
        return parseOtaEntitlement(post(ApiConfig.redeem, JSONObject().put("code", normalized), authorized = true))
    }

    suspend fun firmwareList(): List<Firmware> = withContext(Dispatchers.IO) {
        check(token.isNotEmpty()) { "请先注册或登录" }
        val request = Request.Builder().url(endpoint(ApiConfig.firmware))
            .header("Authorization", "Bearer $token").build()
        http.newCall(request).execute().use { response ->
            if (!response.isSuccessful) apiError(response)
            val items = JSONObject(response.body!!.string()).getJSONArray("items")
            List(items.length()) { i -> items.getJSONObject(i).let {
                Firmware(it.getString("version"), it.getString("product"), it.getString("url"), it.getString("sha256"), it.getLong("size"))
            } }
        }
    }
    suspend fun download(firmware: Firmware, destination: File): File = withContext(Dispatchers.IO) {
        check(token.isNotEmpty()) { "请先注册或登录" }
        require(firmware.model == "exlink-co5300-31pin") { "固件型号不匹配" }
        val downloadUrl = if (firmware.url.startsWith("/")) endpoint(firmware.url) else firmware.url
        require(downloadUrl.startsWith("https://")) { "固件下载必须使用 HTTPS" }
        val downloadOrigin = java.net.URI(downloadUrl)
        val serverOrigin = java.net.URI(baseUrl)
        require(downloadOrigin.scheme == "https" && downloadOrigin.userInfo == null && downloadOrigin.fragment == null &&
            downloadOrigin.host == serverOrigin.host && downloadOrigin.port == serverOrigin.port) { "固件下载地址必须位于账号服务器" }
        require(firmware.size in 1..4_194_304) { "固件大小超过 OTA 分区" }
        require(firmware.sha256.matches(Regex("[0-9a-fA-F]{64}"))) { "固件缺少有效 SHA-256" }
        val digest = MessageDigest.getInstance("SHA-256")
        try {
            http.newCall(Request.Builder().url(downloadUrl).header("Authorization", "Bearer $token").build()).execute().use { response ->
                if (!response.isSuccessful) apiError(response)
                response.body!!.byteStream().use { source -> destination.outputStream().use { sink ->
                    val buffer = ByteArray(8192)
                    var total = 0L
                    while (true) {
                        val n = source.read(buffer)
                        if (n < 0) break
                        total += n
                        check(total <= firmware.size) { "下载大小与清单不符" }
                        digest.update(buffer, 0, n); sink.write(buffer, 0, n)
                    }
                    check(total == firmware.size) { "下载不完整" }
                } }
            }
            check(digest.digest().joinToString("") { "%02x".format(it) }.equals(firmware.sha256, true)) { "固件校验失败" }
            destination
        } catch (e: Exception) { destination.delete(); throw e }
    }
}

data class EmailCodeResult(val retryAfterSeconds: Long, val expiresInSeconds: Long)

package com.exlink.mobile

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import kotlinx.coroutines.runBlocking

class CloudSettingsTest {
    private val fingerprint = "AB".repeat(32)

    private fun invalid(block: () -> Unit) {
        try { block(); fail("Unsafe cloud configuration must be rejected") }
        catch (_: IllegalArgumentException) { }
    }

    @Test fun publicBuildHasNoCloudFallback() {
        val settings = CloudSettings.parse("", "")
        assertFalse(settings.configured)
        assertEquals("", settings.origin)
        assertEquals("", settings.fingerprint)
    }

    @Test fun partialConfigFailsRatherThanChoosingADefaultServer() {
        invalid { CloudSettings.parse("https://192.0.2.10", "") }
        invalid { CloudSettings.parse("", fingerprint) }
    }

    @Test fun unconfiguredClientRejectsCloudRequestsBeforeConnecting() = runBlocking {
        val api = CloudApi("", "")
        assertFalse(api.isConfigured)
        try { api.requestEmailCode("test@example.com"); fail("Unconfigured client must not request email") }
        catch (e: IllegalStateException) { assertTrue(e.message!!.contains("未配置")) }
        api.token = "test-token"
        try { api.otaEntitlement(); fail("Unconfigured client must not request entitlement") }
        catch (e: IllegalStateException) { assertTrue(e.message!!.contains("未配置")) }
    }

    @Test fun privateOriginAndCertificateAreNormalized() {
        val settings = CloudSettings.parse(" https://192.0.2.10:8443/ ", "ab:".repeat(31) + "ab")
        assertTrue(settings.configured)
        assertEquals("https://192.0.2.10:8443", settings.origin)
        assertEquals(fingerprint, settings.fingerprint)
    }

    @Test fun untrustedOriginsAreRejected() {
        listOf("http://192.0.2.10", "https://user@192.0.2.10", "https://192.0.2.10/api",
            "https://192.0.2.10?redirect=1", "https://192.0.2.10#secret", "https://256.0.0.1",
            "https://192.0.2.10:0", "https://192.0.2.10:65536").forEach { origin ->
            invalid { CloudSettings.parse(origin, fingerprint) }
        }
        invalid { CloudSettings.parse("https://192.0.2.10", "A".repeat(63)) }
    }

    @Test fun malformedEntitlementNeverBecomesAuthorized() {
        listOf("{}", "{\"ota_authorized\":\"true\"}", "{\"ota_authorized\":1}",
            "{\"ota_authorized\":null}").forEach { body ->
            try { parseOtaEntitlement(JSONObject(body)); fail("Invalid entitlement must be rejected") }
            catch (_: IllegalStateException) { }
        }
        assertTrue(parseOtaEntitlement(JSONObject("{\"ota_authorized\":true}")))
        assertFalse(parseOtaEntitlement(JSONObject("{\"ota_authorized\":false}")))
    }

    @Test fun authenticationOrForbiddenResponsesInvalidateCachedOtaAccess() {
        assertTrue(CloudApiException(401, "unauthorized").invalidatesOta)
        assertTrue(CloudApiException(403, "ota_not_authorized").invalidatesOta)
        assertTrue(CloudApiException(403, null).invalidatesOta)
        assertFalse(CloudApiException(429, "rate_limited").invalidatesOta)
        assertFalse(CloudApiException(409, "ota_already_authorized").invalidatesOta)
        assertTrue(CloudApiException(400, "ota_code_invalid").message!!.contains("授权码"))
    }
}

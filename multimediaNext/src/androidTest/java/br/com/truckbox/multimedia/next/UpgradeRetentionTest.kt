package br.com.truckbox.multimedia.next

import android.content.Context
import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import br.com.truckbox.multimedia.next.data.AndroidDeviceSigner
import br.com.truckbox.multimedia.next.data.EncryptedRefreshTokenStore
import br.com.truckbox.multimedia.next.data.PreferencesStore
import br.com.truckbox.multimedia.next.data.hash256
import br.com.truckbox.multimedia.next.domain.Appearance
import br.com.truckbox.multimedia.next.domain.LocalPreferences
import br.com.truckbox.multimedia.next.gps.GpsCredentials
import br.com.truckbox.multimedia.next.gps.GpsOutbox
import br.com.truckbox.multimedia.next.gps.GpsPoint
import br.com.truckbox.multimedia.next.gps.GpsSettings
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.Closeable
import java.io.File

@RunWith(AndroidJUnit4::class)
class UpgradeRetentionTest {
    @Test fun retainsExistingProvisioning() {
        val arguments = InstrumentationRegistry.getArguments()
        val phase = arguments.getString("truckboxUpgradePhase")
        assumeTrue(phase == "seed" || phase == "verify")
        assertTrue(Build.HARDWARE in setOf("ranchu", "goldfish"))
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val pkg = context.packageManager.getPackageInfo(context.packageName, 0)
        val expectedVersion = requireNotNull(arguments.getString("expectedVersionCode")).toInt()
        @Suppress("DEPRECATION")
        assertEquals(expectedVersion, pkg.versionCode)

        val marker = File(context.noBackupFilesDir, "upgrade-test-state.json")
        val gps = GpsSettings(context)
        val cloud = EncryptedRefreshTokenStore(context, BuildConfig.CLOUD_URL + "|" + BuildConfig.CLOUD_PUBLISHABLE_KEY)
        val preferences = PreferencesStore(context)
        if (phase == "seed") {
            assertTrue(!marker.exists())
            assertNull(cloud.read())
            assertNull(gps.credentials())
            assertTrue(cloud.writeIfCurrent(cloud.generation(), cloudToken))
            assertTrue(gps.save(GpsCredentials(gpsUid, gpsToken)))
            gps.enabled = true
            preferences.save(savedPreferences)
        }

        val signer = AndroidDeviceSigner()
        assertTrue(cloud.read()?.token == cloudToken)
        assertTrue(gps.credentials()?.token == gpsToken)
        assertEquals(gpsUid, gps.deviceUid)
        assertTrue(gps.enabled)
        assertEquals(savedPreferences, preferences.read())
        val outbox = GpsOutbox(context)
        try {
            if (phase == "seed") {
                assertEquals(0L, outbox.pending(gpsUid))
                assertTrue(outbox.enqueue(gpsUid, point))
            }
            if (expectedVersion >= 9) assertTrue(Closeable::class.java.isInstance(outbox))
            assertEquals(1L, outbox.pending(gpsUid))
            assertEquals(point, outbox.oldest(gpsUid))
        } finally {
            outbox.close()
        }
        val encryptedCloud = context.getSharedPreferences("multimedia_next_cloud_secure_v1", Context.MODE_PRIVATE)
            .getString("refresh", null)
        val snapshot = JSONObject()
            .put("uid", context.applicationInfo.uid)
            .put("gpsInstallId", gps.installId)
            .put("gpsCiphertext", hash256(File(context.noBackupFilesDir, "gps-credentials.json").readBytes()))
            .put("cloudCiphertext", hash256(requireNotNull(encryptedCloud).toByteArray()))
            .put("devicePublicKey", hash256(signer.publicKeySpki.toByteArray()))
            .put("generation", cloud.generation())
            .toString()
        if (phase == "seed") marker.writeText(snapshot) else assertTrue(marker.readText() == snapshot)
    }

    private companion object {
        val cloudToken = "tbxft_" + "a".repeat(64)
        const val gpsUid = "TRUCKBOX-UPGRADE-TEST"
        const val gpsToken = "GPS-UPGRADE-TEST"
        val savedPreferences = LocalPreferences(Appearance.NIGHT, "192.168.4.2")
        val point = GpsPoint("upgrade-test-point", 1_700_000_000_000L, "{}")
    }
}

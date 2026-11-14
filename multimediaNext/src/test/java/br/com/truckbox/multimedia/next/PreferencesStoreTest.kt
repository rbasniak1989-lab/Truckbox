package br.com.truckbox.multimedia.next

import android.content.Context
import br.com.truckbox.multimedia.next.data.PreferencesStore
import br.com.truckbox.multimedia.next.domain.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[35])
class PreferencesStoreTest {
    @Test fun localAppearanceSurvivesNewStoreAndContainsNoPolicyOrProtectedCache() {
        val context=RuntimeEnvironment.getApplication()
        val expected=LocalPreferences(Appearance.NIGHT,"192.168.4.1")
        PreferencesStore(context).save(expected)
        assertEquals(expected,PreferencesStore(context).read())
        val keys=context.getSharedPreferences("multimedia_next_local_v1",Context.MODE_PRIVATE).all.keys
        assertEquals(setOf("appearance","core_host"),keys)
    }
}

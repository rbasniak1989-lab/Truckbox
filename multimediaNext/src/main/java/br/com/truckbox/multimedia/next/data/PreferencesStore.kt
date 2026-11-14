package br.com.truckbox.multimedia.next.data

import android.content.Context
import androidx.core.content.edit
import br.com.truckbox.multimedia.next.domain.*

/** Somente aparência e host local. Não há permissão, token ou dado protegido neste arquivo. */
class PreferencesStore(context: Context) {
    private val store = context.getSharedPreferences("multimedia_next_local_v1",Context.MODE_PRIVATE)
    fun read() = LocalPreferences(
        appearance = runCatching { Appearance.valueOf(store.getString("appearance",Appearance.AUTO.name)!!) }.getOrDefault(Appearance.AUTO),
        coreHost = store.getString("core_host","truckbox.local").takeIf { it != null && validCoreHost(it) } ?: "truckbox.local",
    )
    fun save(preferences: LocalPreferences) {
        require(validCoreHost(preferences.coreHost))
        store.edit {
            putString("appearance",preferences.appearance.name)
            putString("core_host",preferences.coreHost)
        }
    }
}

// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-3.0-or-later

package dev.lemon.lemon_emu.updater

import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import dev.lemon.lemon_emu.utils.DirectoryInitialization
import dev.lemon.lemon_emu.utils.Log
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import org.json.JSONObject
import kotlin.coroutines.resume

/**
 * No patch ships inside the app. The newest patches-<serial> release of GRID0plus-cfw is the one
 * source for the console Toolbox and every GRID0 emulator (cfw repo, patches/README.md); the
 * native loader reads <user dir>/grid0plus/patches (FileSys::PrivateServerPatchesDir).
 *
 * At every launch the SHA-256 of each file there is compared with the release manifest. If
 * anything differs the player is asked, and on yes every changed file is downloaded and checked
 * against the manifest before anything is written. Patches are applied when a game boots, and no
 * game is running at launch, so unlike the desktop emulators nothing needs a restart. The folder
 * is exclusively GRID0+'s, so files the manifest does not list are deleted.
 */
object Grid0PatchUpdater {
    private const val RELEASES_API =
        "https://api.github.com/repos/GRID0-net/GRID0plus-cfw/releases?per_page=30"
    private const val DOWNLOAD_BASE =
        "https://github.com/GRID0-net/GRID0plus-cfw/releases/download/"

    private val PATCHES_TAG = Regex("^patches-([0-9]+)$")
    private val SAFE_DEST = Regex("^exefs_patches/[A-Za-z0-9_.-]+/[A-Za-z0-9]+\\.ips$")

    private data class Entry(val dest: String, val sha256: String, val asset: String)

    private class Plan(val tag: String, val needed: List<Entry>, val stale: List<File>)

    private val client = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .build()

    private val root: File
        get() = File(DirectoryInitialization.userDirectory!!, "grid0plus/patches")

    fun checkOnLaunch(activity: AppCompatActivity) {
        activity.lifecycleScope.launch {
            try {
                val plan = withContext(Dispatchers.IO) { plan() }
                if (plan.needed.isEmpty() && plan.stale.isEmpty()) {
                    Log.info("[GRID0] Patches current (${plan.tag})")
                    return@launch
                }
                if (!ask(activity, plan)) {
                    return@launch
                }
                withContext(Dispatchers.IO) { install(plan) }
                Log.info("[GRID0] Patches updated to ${plan.tag}")
            } catch (e: Exception) {
                Log.warning("[GRID0] Patch check failed: ${e.message}")
                if (!File(root, "exefs_patches").isDirectory && !activity.isFinishing) {
                    MaterialAlertDialogBuilder(activity)
                        .setTitle("GRID0+ patches")
                        .setMessage(
                            "The GRID0+ patches could not be downloaded. Splatoon 3 cannot " +
                                "connect to GRID0+ without them; the app tries again every " +
                                "time it starts."
                        )
                        .setPositiveButton(android.R.string.ok, null)
                        .show()
                }
            }
        }
    }

    private fun get(url: String): ByteArray {
        val request = Request.Builder().url(url).header("User-Agent", "GRID0-citrosis-android").build()
        client.newCall(request).execute().use { response ->
            if (!response.isSuccessful) {
                throw IllegalStateException("$url answered ${response.code}")
            }
            return response.body!!.bytes()
        }
    }

    private fun sha256(data: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(data).joinToString("") { "%02x".format(it) }

    private fun plan(): Plan {
        val releases = JSONArray(String(get(RELEASES_API)))
        var tag: String? = null
        var best = -1
        for (i in 0 until releases.length()) {
            val release = releases.getJSONObject(i)
            if (release.optBoolean("draft")) continue
            val match = PATCHES_TAG.matchEntire(release.optString("tag_name")) ?: continue
            val serial = match.groupValues[1].toInt()
            if (serial > best) {
                best = serial
                tag = match.value
            }
        }
        tag ?: throw IllegalStateException("no patches release found")

        val manifest = JSONObject(String(get("$DOWNLOAD_BASE$tag/manifest.json")))
        val patches = manifest.getJSONArray("patches")
        val entries = mutableListOf<Entry>()
        for (i in 0 until patches.length()) {
            val p = patches.getJSONObject(i)
            val targets = p.optJSONArray("targets") ?: continue
            if ((0 until targets.length()).none { targets.optString(it) == "emulator" }) continue
            val e = Entry(p.optString("dest"), p.optString("sha256"), p.optString("asset"))
            // Any malformed entry rejects the whole manifest.
            if (!SAFE_DEST.matches(e.dest) || e.sha256.length != 64 || e.asset != e.sha256 + ".ips") {
                throw IllegalStateException("malformed manifest entry ${e.dest}")
            }
            entries += e
        }
        if (entries.isEmpty()) throw IllegalStateException("manifest lists no emulator patches")

        val listed = entries.map { File(root, it.dest).canonicalPath }.toSet()
        val needed = entries.filter {
            val f = File(root, it.dest)
            !f.isFile || sha256(f.readBytes()) != it.sha256
        }
        val stale = root.walkTopDown().filter { it.isFile && it.canonicalPath !in listed }.toList()
        return Plan(tag, needed, stale)
    }

    private suspend fun ask(activity: AppCompatActivity, plan: Plan): Boolean =
        suspendCancellableCoroutine { cont ->
            if (activity.isFinishing) {
                cont.resume(false)
                return@suspendCancellableCoroutine
            }
            val changes = plan.needed.size + plan.stale.size
            MaterialAlertDialogBuilder(activity)
                .setTitle("GRID0+ patch update")
                .setMessage(
                    "$changes GRID0+ patch file${if (changes == 1) "" else "s"} changed " +
                        "(${plan.tag}).\n\nDownload now?"
                )
                .setPositiveButton("Download") { _, _ -> if (cont.isActive) cont.resume(true) }
                .setNegativeButton("Later") { _, _ -> if (cont.isActive) cont.resume(false) }
                .setOnCancelListener { if (cont.isActive) cont.resume(false) }
                .show()
        }

    private fun install(plan: Plan) {
        // Everything is downloaded and checked before the first file is written.
        val files = plan.needed.map { e ->
            val data = get("$DOWNLOAD_BASE${plan.tag}/${e.asset}")
            if (sha256(data) != e.sha256) {
                throw IllegalStateException("${e.asset} does not match its manifest hash")
            }
            e to data
        }
        for ((e, data) in files) {
            val target = File(root, e.dest)
            target.parentFile!!.mkdirs()
            val tmp = File(target.path + ".tmp")
            tmp.writeBytes(data)
            if (!tmp.renameTo(target)) {
                tmp.delete()
                throw IllegalStateException("could not write ${e.dest}")
            }
        }
        plan.stale.forEach { it.delete() }
        // Directories emptied by a removal, deepest first.
        root.walkBottomUp().filter { it != root && it.isDirectory && it.list()?.isEmpty() == true }
            .forEach { it.delete() }
    }
}

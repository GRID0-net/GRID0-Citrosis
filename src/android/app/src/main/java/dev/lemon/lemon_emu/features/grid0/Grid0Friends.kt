// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Citrosis: GRID0+ friends data for the Android Friends screen. The native side
// (jni/citrosis_android.cpp) answers with the same JSON the macOS app reads.

package dev.lemon.lemon_emu.features.grid0

import dev.lemon.lemon_emu.NativeLibrary
import org.json.JSONArray
import org.json.JSONObject

data class Grid0Friend(
    val id: String,
    val name: String,
    /** ONLINE, PLAYING, OFFLINE, or empty when the friend hides their presence. */
    val state: String,
    val appId: String,
    val code: String,
    val favorite: Boolean,
    /** Local path of the cached profile picture, or empty. */
    val icon: String
) {
    val isOnline get() = state == "ONLINE" || state == "PLAYING"

    /** Playing first, then online, then the rest; favorites first within each. */
    val rank get() = (if (state == "PLAYING") 0 else if (state == "ONLINE") 2 else 4) +
        if (favorite) 0 else 1
}

data class Grid0Request(val id: String, val other: Grid0Friend, val incoming: Boolean)

data class Grid0Snapshot(
    val myName: String,
    val myCode: String,
    val friends: List<Grid0Friend>,
    val requests: List<Grid0Request>,
    val warning: String?
)

sealed class Grid0Result {
    data class Ok(val snapshot: Grid0Snapshot) : Grid0Result()
    object NotSignedIn : Grid0Result()
    data class Failed(val message: String) : Grid0Result()
}

object Grid0Friends {
    /** Blocking: signs in, fetches friends (with pictures) and requests. */
    fun load(): Grid0Result {
        val json = try {
            JSONObject(NativeLibrary.grid0Friends())
        } catch (e: Exception) {
            return Grid0Result.Failed(e.message ?: "Could not read the GRID0+ response")
        }
        json.optString("error").takeIf { it.isNotEmpty() }?.let {
            return if (it == "not_signed_in") Grid0Result.NotSignedIn else Grid0Result.Failed(it)
        }
        val me = json.optJSONObject("me")
        val requests = parseRequests(json.optJSONArray("incoming"), true) +
            parseRequests(json.optJSONArray("outgoing"), false)
        return Grid0Result.Ok(
            Grid0Snapshot(
                myName = me?.optString("name") ?: "",
                myCode = me?.optString("code") ?: "",
                friends = parseFriends(json.optJSONArray("friends")).sortedBy { it.rank },
                requests = requests,
                warning = json.optString("warning").takeIf { it.isNotEmpty() }
            )
        )
    }

    /** Blocking. Returns an error message for the player, or null on success. */
    fun action(id: String, action: String): String? =
        NativeLibrary.grid0FriendAction(id, action).takeIf { it.isNotEmpty() }

    fun formatFriendCode(code: String): String {
        val digits = code.filter { it.isDigit() }
        if (digits.length != 12) return code
        return "SW-${digits.substring(0, 4)}-${digits.substring(4, 8)}-${digits.substring(8)}"
    }

    private fun parseFriends(array: JSONArray?): List<Grid0Friend> {
        if (array == null) return emptyList()
        return (0 until array.length()).map { parseFriend(array.getJSONObject(it)) }
    }

    private fun parseFriend(node: JSONObject) = Grid0Friend(
        id = node.optString("id"),
        name = node.optString("name"),
        state = node.optString("state"),
        appId = node.optString("app"),
        code = node.optString("code"),
        favorite = node.optBoolean("favorite"),
        icon = node.optString("icon")
    )

    private fun parseRequests(array: JSONArray?, incoming: Boolean): List<Grid0Request> {
        if (array == null) return emptyList()
        return (0 until array.length()).map {
            val node = array.getJSONObject(it)
            Grid0Request(
                node.optString("id"),
                parseFriend(node.optJSONObject("other") ?: JSONObject()),
                incoming
            )
        }
    }
}

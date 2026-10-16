// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package dev.lemon.lemon_emu.features.grid0

import android.annotation.SuppressLint
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.view.isVisible
import androidx.recyclerview.widget.RecyclerView
import java.io.File
import dev.lemon.lemon_emu.R
import dev.lemon.lemon_emu.databinding.ItemGrid0FriendBinding
import dev.lemon.lemon_emu.databinding.ItemGrid0MessageBinding

sealed class Grid0Row {
    data class FriendRow(val friend: Grid0Friend) : Grid0Row()
    data class RequestRow(val request: Grid0Request) : Grid0Row()
    data class Message(val text: String, val actionText: String? = null) : Grid0Row()
}

class Grid0FriendsAdapter(
    private val onFriendClick: (Grid0Friend) -> Unit,
    private val onRequestAction: (Grid0Request, String) -> Unit,
    private val onMessageAction: () -> Unit
) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
    private var rows: List<Grid0Row> = emptyList()

    @SuppressLint("NotifyDataSetChanged")
    fun submit(newRows: List<Grid0Row>) {
        rows = newRows
        notifyDataSetChanged()
    }

    override fun getItemCount() = rows.size

    override fun getItemViewType(position: Int) = if (rows[position] is Grid0Row.Message) 1 else 0

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        return if (viewType == 1) {
            MessageHolder(ItemGrid0MessageBinding.inflate(inflater, parent, false))
        } else {
            PersonHolder(ItemGrid0FriendBinding.inflate(inflater, parent, false))
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        when (val row = rows[position]) {
            is Grid0Row.FriendRow -> (holder as PersonHolder).bindFriend(row.friend)
            is Grid0Row.RequestRow -> (holder as PersonHolder).bindRequest(row.request)
            is Grid0Row.Message -> (holder as MessageHolder).bind(row)
        }
    }

    private inner class MessageHolder(val binding: ItemGrid0MessageBinding) :
        RecyclerView.ViewHolder(binding.root) {
        fun bind(row: Grid0Row.Message) {
            binding.textMessage.text = row.text
            binding.buttonMessageAction.isVisible = row.actionText != null
            binding.buttonMessageAction.text = row.actionText
            binding.buttonMessageAction.setOnClickListener { onMessageAction() }
        }
    }

    private inner class PersonHolder(val binding: ItemGrid0FriendBinding) :
        RecyclerView.ViewHolder(binding.root) {
        private fun bindPerson(person: Grid0Friend, statusText: String) {
            binding.textName.text = if (person.favorite) "★ ${person.name}" else person.name
            binding.textStatus.text = statusText
            val bitmap = person.icon.takeIf { it.isNotEmpty() && File(it).exists() }
                ?.let { BitmapFactory.decodeFile(it) }
            binding.imageAvatar.setImageBitmap(bitmap)
            binding.textInitial.isVisible = bitmap == null
            binding.textInitial.text = person.name.firstOrNull()?.uppercase() ?: "?"
            (binding.viewStatusDot.background?.mutate() as? GradientDrawable)
                ?.setColor(statusColor(person))
        }

        fun bindFriend(friend: Grid0Friend) {
            bindPerson(friend, statusText(friend))
            binding.viewStatusDot.isVisible = true
            binding.buttonPrimary.isVisible = false
            binding.buttonSecondary.isVisible = false
            binding.root.setOnClickListener { onFriendClick(friend) }
        }

        fun bindRequest(request: Grid0Request) {
            val context = binding.root.context
            bindPerson(
                request.other,
                context.getString(
                    if (request.incoming) {
                        R.string.grid0_friends_request_from
                    } else {
                        R.string.grid0_friends_request_to
                    },
                    Grid0Friends.formatFriendCode(request.other.code)
                )
            )
            binding.viewStatusDot.isVisible = false
            binding.root.setOnClickListener(null)
            binding.root.isClickable = false
            if (request.incoming) {
                binding.buttonPrimary.isVisible = true
                binding.buttonPrimary.setText(R.string.grid0_friends_accept)
                binding.buttonPrimary.setOnClickListener { onRequestAction(request, "accept") }
                binding.buttonSecondary.isVisible = true
                binding.buttonSecondary.setText(R.string.grid0_friends_decline)
                binding.buttonSecondary.setOnClickListener { onRequestAction(request, "deny") }
            } else {
                binding.buttonPrimary.isVisible = false
                binding.buttonSecondary.isVisible = true
                binding.buttonSecondary.setText(android.R.string.cancel)
                binding.buttonSecondary.setOnClickListener { onRequestAction(request, "cancel") }
            }
        }

        private fun statusText(friend: Grid0Friend): String {
            val context = binding.root.context
            return when (friend.state) {
                "PLAYING" -> gameName(friend.appId)?.let {
                    context.getString(R.string.grid0_friends_playing_game, it)
                } ?: context.getString(R.string.grid0_friends_playing)
                "ONLINE" -> context.getString(R.string.grid0_friends_online)
                "" -> context.getString(R.string.grid0_friends_hidden)
                else -> context.getString(R.string.grid0_friends_offline)
            }
        }
    }

    companion object {
        fun gameName(appId: String): String? = when (appId.lowercase()) {
            "0100c2500fc20000" -> "Splatoon 3"
            "01003bc0000a0000" -> "Splatoon 2"
            "" -> null
            else -> appId
        }

        fun statusColor(friend: Grid0Friend): Int = when (friend.state) {
            "PLAYING" -> Color.rgb(0x2e, 0xcc, 0x71)
            "ONLINE" -> Color.rgb(0x34, 0x98, 0xdb)
            else -> Color.rgb(0x7f, 0x8c, 0x8d)
        }
    }
}

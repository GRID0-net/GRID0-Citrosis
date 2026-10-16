// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Citrosis: GRID0+ Friends. Same features as the Qt Friends window and the macOS app: your name
// and friend code, the friend list with presence and pictures, incoming and outgoing requests,
// and adding, accepting, declining, cancelling and removing friends.

package dev.lemon.lemon_emu.fragments

import android.content.ClipData
import android.content.ClipboardManager
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.Toast
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.isVisible
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.navigation.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.tabs.TabLayout
import com.google.android.material.transition.MaterialSharedAxis
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import android.content.Intent
import dev.lemon.lemon_emu.features.settings.ui.SettingsActivity
import dev.lemon.lemon_emu.features.settings.ui.SettingsActivityArgs
import dev.lemon.lemon_emu.R
import dev.lemon.lemon_emu.databinding.FragmentGrid0FriendsBinding
import dev.lemon.lemon_emu.features.grid0.Grid0Friend
import dev.lemon.lemon_emu.features.grid0.Grid0Friends
import dev.lemon.lemon_emu.features.grid0.Grid0FriendsAdapter
import dev.lemon.lemon_emu.features.grid0.Grid0Request
import dev.lemon.lemon_emu.features.grid0.Grid0Result
import dev.lemon.lemon_emu.features.grid0.Grid0Row
import dev.lemon.lemon_emu.features.grid0.Grid0Snapshot
import dev.lemon.lemon_emu.features.settings.model.Settings
import dev.lemon.lemon_emu.model.HomeViewModel
import dev.lemon.lemon_emu.utils.ViewUtils.updateMargins

class Grid0FriendsFragment : Fragment() {
    private var _binding: FragmentGrid0FriendsBinding? = null
    private val binding get() = _binding!!

    private lateinit var adapter: Grid0FriendsAdapter

    private var snapshot: Grid0Snapshot? = null
    private var lastResult: Grid0Result? = null
    private var loading = false
    private var busy = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentGrid0FriendsBinding.inflate(inflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        binding.toolbarFriends.setNavigationOnClickListener {
            requireActivity().finish()
        }
        binding.toolbarFriends.inflateMenu(R.menu.menu_grid0_friends)
        binding.toolbarFriends.setOnMenuItemClickListener {
            when (it.itemId) {
                R.id.menu_friends_refresh -> {
                    refresh()
                    true
                }
                R.id.menu_friends_settings -> {
                    openAccountSettings()
                    true
                }
                else -> false
            }
        }

        adapter = Grid0FriendsAdapter(
            onFriendClick = { showFriendDialog(it) },
            onRequestAction = { request, action -> settle(request, action) },
            onMessageAction = { openAccountSettings() }
        )
        binding.listFriends.layoutManager = LinearLayoutManager(requireContext())
        binding.listFriends.adapter = adapter

        binding.tabsFriends.addTab(binding.tabsFriends.newTab().setText(R.string.grid0_friends_tab))
        binding.tabsFriends.addTab(
            binding.tabsFriends.newTab().setText(R.string.grid0_friends_requests_tab)
        )
        binding.tabsFriends.addOnTabSelectedListener(object : TabLayout.OnTabSelectedListener {
            override fun onTabSelected(tab: TabLayout.Tab) = render()
            override fun onTabUnselected(tab: TabLayout.Tab) {}
            override fun onTabReselected(tab: TabLayout.Tab) {}
        })

        binding.buttonCopyCode.setOnClickListener { copyMyCode() }
        binding.buttonAddFriend.setOnClickListener { addFriend() }
        binding.editFriendCode.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_SEND) {
                addFriend()
                true
            } else {
                false
            }
        }

        setInsets()
        render()

        // Refresh now and every 15 s while the screen is visible, like the desktop window.
        viewLifecycleOwner.lifecycleScope.launch {
            viewLifecycleOwner.repeatOnLifecycle(Lifecycle.State.STARTED) {
                while (true) {
                    refresh()
                    delay(15_000)
                }
            }
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun refresh() {
        if (loading) return
        loading = true
        updateProgress()
        viewLifecycleOwner.lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) { Grid0Friends.load() }
            loading = false
            lastResult = result
            if (result is Grid0Result.Ok) {
                snapshot = result.snapshot
                result.snapshot.warning?.let { toast(it) }
            }
            render()
        }
    }

    private fun render() {
        if (_binding == null) return
        updateProgress()
        val result = lastResult
        val data = snapshot
        val signedIn = data != null
        binding.rowAddFriend.isVisible = result !is Grid0Result.NotSignedIn
        binding.buttonAddFriend.isEnabled = signedIn && !busy
        binding.buttonCopyCode.isEnabled = signedIn

        when {
            result is Grid0Result.NotSignedIn -> {
                binding.textMyName.setText(R.string.grid0_friends_not_signed_in)
                binding.textMyCode.setText(R.string.grid0_friends_not_signed_in_description)
                binding.tabsFriends.isVisible = false
                adapter.submit(
                    listOf(
                        Grid0Row.Message(
                            getString(R.string.grid0_friends_not_signed_in_body),
                            getString(R.string.grid0_friends_account_settings)
                        )
                    )
                )
                return
            }
            data == null && result is Grid0Result.Failed -> {
                binding.textMyName.setText(R.string.grid0_friends_sign_in_failed)
                binding.textMyCode.text = result.message
                binding.tabsFriends.isVisible = false
                adapter.submit(
                    listOf(
                        Grid0Row.Message(
                            result.message,
                            getString(R.string.grid0_friends_account_settings)
                        )
                    )
                )
                return
            }
            data == null -> {
                binding.textMyName.setText(R.string.grid0_friends_signing_in)
                binding.textMyCode.text = ""
                binding.tabsFriends.isVisible = false
                adapter.submit(emptyList())
                return
            }
        }

        val snap = data ?: return
        binding.tabsFriends.isVisible = true
        binding.textMyName.text = snap.myName
        binding.textMyCode.text =
            getString(R.string.grid0_friends_my_code, Grid0Friends.formatFriendCode(snap.myCode))

        val online = snap.friends.count { it.isOnline }
        val incoming = snap.requests.count { it.incoming }
        binding.tabsFriends.getTabAt(0)?.text = getString(R.string.grid0_friends_tab_count, online)
        binding.tabsFriends.getTabAt(1)?.text = if (incoming > 0) {
            getString(R.string.grid0_friends_requests_tab_count, incoming)
        } else {
            getString(R.string.grid0_friends_requests_tab)
        }

        val rows = if (binding.tabsFriends.selectedTabPosition == 1) {
            snap.requests.sortedBy { !it.incoming }.map { Grid0Row.RequestRow(it) }
                .ifEmpty { listOf(Grid0Row.Message(getString(R.string.grid0_friends_no_requests))) }
        } else {
            snap.friends.map { Grid0Row.FriendRow(it) }
                .ifEmpty { listOf(Grid0Row.Message(getString(R.string.grid0_friends_no_friends))) }
        }
        adapter.submit(rows)
    }

    private fun updateProgress() {
        if (_binding == null) return
        binding.progressFriends.isVisible = loading || busy
    }

    private fun addFriend() {
        val code = binding.editFriendCode.text?.toString()?.trim() ?: ""
        if (code.isEmpty() || busy) return
        runAction(code, "send", getString(R.string.grid0_friends_request_sent)) {
            binding.editFriendCode.text = null
        }
    }

    private fun settle(request: Grid0Request, action: String) =
        runAction(request.id, action, null)

    private fun runAction(
        id: String,
        action: String,
        successMessage: String?,
        onSuccess: () -> Unit = {}
    ) {
        if (busy) return
        busy = true
        render()
        viewLifecycleOwner.lifecycleScope.launch {
            val error = withContext(Dispatchers.IO) { Grid0Friends.action(id, action) }
            busy = false
            if (error != null) {
                if (_binding != null) {
                    MaterialAlertDialogBuilder(requireContext())
                        .setTitle(R.string.grid0_friends_title)
                        .setMessage(error)
                        .setPositiveButton(android.R.string.ok, null)
                        .show()
                }
            } else {
                successMessage?.let { toast(it) }
                if (_binding != null) onSuccess()
            }
            render()
            refresh()
        }
    }

    private fun showFriendDialog(friend: Grid0Friend) {
        val code = Grid0Friends.formatFriendCode(friend.code)
        MaterialAlertDialogBuilder(requireContext())
            .setTitle(friend.name)
            .setMessage(getString(R.string.grid0_friends_friend_details, code))
            .setPositiveButton(R.string.grid0_friends_copy_code) { _, _ -> copy(code) }
            .setNegativeButton(R.string.grid0_friends_remove) { _, _ -> confirmRemove(friend) }
            .setNeutralButton(android.R.string.cancel, null)
            .show()
    }

    private fun confirmRemove(friend: Grid0Friend) {
        MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.grid0_friends_remove)
            .setMessage(getString(R.string.grid0_friends_remove_confirm, friend.name))
            .setPositiveButton(R.string.grid0_friends_remove) { _, _ ->
                runAction(friend.id, "remove", getString(R.string.grid0_friends_removed, friend.name))
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun copyMyCode() {
        snapshot?.let { copy(Grid0Friends.formatFriendCode(it.myCode)) }
    }

    private fun copy(text: String) {
        val clipboard = requireContext().getSystemService(ClipboardManager::class.java)
        clipboard?.setPrimaryClip(ClipData.newPlainText("GRID0+ friend code", text))
        toast(getString(R.string.grid0_friends_code_copied))
    }

    private fun openAccountSettings() {
        startActivity(Intent(requireContext(), SettingsActivity::class.java).apply {
            putExtras(SettingsActivityArgs(null, Settings.MenuTag.SECTION_ONLINE).toBundle())
        })
    }

    private fun toast(message: String) {
        context?.let { Toast.makeText(it, message, Toast.LENGTH_SHORT).show() }
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { _, windowInsets ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right
            binding.toolbarFriends.updateMargins(left = leftInsets, right = rightInsets)
            binding.containerHeader.updateMargins(left = leftInsets, right = rightInsets)
            binding.listFriends.updateMargins(left = leftInsets, right = rightInsets)
            binding.listFriends.updatePadding(bottom = barInsets.bottom)
            windowInsets
        }
}

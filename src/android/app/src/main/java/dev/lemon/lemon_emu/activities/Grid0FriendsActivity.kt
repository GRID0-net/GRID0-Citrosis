// SPDX-FileCopyrightText: Copyright 2026 Citrosis contributors
// SPDX-License-Identifier: GPL-2.0-or-later
package dev.lemon.lemon_emu.activities

import android.content.Context
import android.os.Bundle
import dev.lemon.lemon_emu.LemonApplication
import dev.lemon.lemon_emu.utils.ThemeHelper
import android.widget.FrameLayout
import androidx.appcompat.app.AppCompatActivity
import dev.lemon.lemon_emu.fragments.Grid0FriendsFragment
import dev.lemon.lemon_emu.R

class Grid0FriendsActivity : AppCompatActivity() {
    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(LemonApplication.applyLanguage(base))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        ThemeHelper.setTheme(this)
        super.onCreate(savedInstanceState)
        val frame = FrameLayout(this).apply { id = R.id.grid0_friends_container }
        setContentView(frame)
        if (savedInstanceState == null) supportFragmentManager.beginTransaction()
            .replace(R.id.grid0_friends_container, Grid0FriendsFragment()).commit()
    }
}

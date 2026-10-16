// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package dev.lemon.lemon_emu.features.settings.model

import dev.lemon.lemon_emu.utils.NativeConfig

enum class StringSetting(override val key: String) : AbstractStringSetting {
    NETWORK_INTERFACE("network_interface"),
    SWITCHNET_USERNAME("switchnet_username"),
    SWITCHNET_PASSWORD("switchnet_password"),
    SWITCHNET_SERVER("switchnet_server"),
    PRIVATE_SERVER_ADDRESS("private_server_address"),
    PRIVATE_SERVER_NAT_SECONDARY_ADDRESS("private_server_nat_secondary_address"),
    PRIVATE_SERVER_CA_BUNDLE("private_server_ca_bundle"),
    DRIVER_PATH("driver_path"),
    DEVICE_NAME("device_name"),
    LOG_FILTER("log_filter"),
    PROGRAM_ARGS("program_args"),
    POST_SHADER_CHAIN("post_shader_chain"),

    WEB_TOKEN("lemon_token"),
    WEB_USERNAME("lemon_username")
    ;

    override fun getString(needsGlobal: Boolean): String = NativeConfig.getString(key, needsGlobal)

    override fun setString(value: String) {
        if (NativeConfig.isPerGameConfigLoaded()) {
            global = false
        }
        NativeConfig.setString(key, value)
    }

    override val defaultValue: String by lazy { NativeConfig.getDefaultToString(key) }

    override fun getValueAsString(needsGlobal: Boolean): String = getString(needsGlobal)

    override fun reset() = NativeConfig.setString(key, defaultValue)
}

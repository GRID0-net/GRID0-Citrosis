// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package dev.lemon.lemon_emu.utils

import android.content.Context
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiManager
import java.net.Inet4Address
import dev.lemon.lemon_emu.NativeLibrary

/**
 * Citrosis: tells the emulator which gateway each network interface has.
 *
 * Android does not let apps read /proc/net/route, so on its own the emulator sees every
 * interface without a default gateway. That breaks picking the right adapter automatically and
 * makes NIFM report odd values to games. ConnectivityManager knows the routes of every network,
 * including VPN-style overlays such as ZeroTier (tun0) and Tailscale, so this pushes them down.
 *
 * It also holds a Wi-Fi multicast lock while a game runs: many devices filter broadcast and
 * multicast frames in power save, which hides LAN-mode sessions on the same Wi-Fi.
 */
object NetworkMonitor {
    private val links = HashMap<Network, LinkProperties>()
    private var registered = false
    private var multicastLock: WifiManager.MulticastLock? = null

    private val callback = object : ConnectivityManager.NetworkCallback() {
        override fun onLinkPropertiesChanged(network: Network, linkProperties: LinkProperties) {
            synchronized(links) { links[network] = linkProperties }
            publish()
        }

        override fun onLost(network: Network) {
            synchronized(links) { links.remove(network) }
            publish()
        }
    }

    fun start(context: Context) {
        if (registered) return
        val manager = context.getSystemService(ConnectivityManager::class.java) ?: return
        // Every network, VPNs and LAN-only networks included (NOT_VPN and INTERNET are default
        // capabilities of a request).
        val request = NetworkRequest.Builder()
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .build()
        try {
            manager.registerNetworkCallback(request, callback)
            registered = true
        } catch (e: Exception) {
            Log.warning("[NetworkMonitor] Could not watch networks: ${e.message}")
        }
    }

    private fun publish() {
        val names = ArrayList<String>()
        val gateways = ArrayList<String>()
        synchronized(links) {
            for (properties in links.values) {
                val name = properties.interfaceName ?: continue
                val gateway = properties.routes.firstOrNull { route ->
                    route.isDefaultRoute && route.gateway is Inet4Address &&
                        !route.gateway!!.isAnyLocalAddress
                }?.gateway?.hostAddress ?: continue
                names.add(name)
                gateways.add(gateway)
            }
        }
        try {
            NativeLibrary.setNetworkGateways(names.toTypedArray(), gateways.toTypedArray())
        } catch (e: UnsatisfiedLinkError) {
            // Native library not loaded yet; the next change publishes again.
        }
    }

    /** Called when a game starts. */
    fun onEmulationStarted(context: Context) {
        if (multicastLock?.isHeld == true) return
        val wifi = context.applicationContext.getSystemService(WifiManager::class.java) ?: return
        try {
            multicastLock = wifi.createMulticastLock("Citrosis LAN").apply {
                setReferenceCounted(false)
                acquire()
            }
        } catch (e: Exception) {
            Log.warning("[NetworkMonitor] No multicast lock: ${e.message}")
        }
    }

    /** Called when a game stops. */
    fun onEmulationStopped() {
        try {
            multicastLock?.takeIf { it.isHeld }?.release()
        } catch (_: Exception) {
        }
        multicastLock = null
    }
}

// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.phoenix.phoenix_emu.features.input

import android.content.Context
import android.os.Build
import android.os.CombinedVibration
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.InputDevice
import androidx.annotation.Keep
import androidx.annotation.RequiresApi
import org.phoenix.phoenix_emu.PhoenixApplication

@Keep
@Suppress("DEPRECATION")
interface PhoenixVibrator {
    fun supportsVibration(): Boolean

    fun vibrate(intensity: Float)

    companion object {
        fun getControllerVibrator(device: InputDevice): PhoenixVibrator =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                PhoenixVibratorManager(device.vibratorManager)
            } else {
                PhoenixVibratorManagerCompat(device.vibrator)
            }

        fun getSystemVibrator(): PhoenixVibrator =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vibratorManager = PhoenixApplication.appContext
                    .getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
                PhoenixVibratorManager(vibratorManager)
            } else {
                val vibrator = PhoenixApplication.appContext
                    .getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
                PhoenixVibratorManagerCompat(vibrator)
            }

        fun getVibrationEffect(intensity: Float): VibrationEffect? {
            if (intensity > 0f) {
                return VibrationEffect.createOneShot(
                    50,
                    (255.0 * intensity).toInt().coerceIn(1, 255)
                )
            }
            return null
        }
    }
}

@RequiresApi(Build.VERSION_CODES.S)
class PhoenixVibratorManager(private val vibratorManager: VibratorManager) : PhoenixVibrator {
    override fun supportsVibration(): Boolean {
        return vibratorManager.vibratorIds.isNotEmpty()
    }

    override fun vibrate(intensity: Float) {
        val vibration = PhoenixVibrator.getVibrationEffect(intensity) ?: return
        vibratorManager.vibrate(CombinedVibration.createParallel(vibration))
    }
}

class PhoenixVibratorManagerCompat(private val vibrator: Vibrator) : PhoenixVibrator {
    override fun supportsVibration(): Boolean {
        return vibrator.hasVibrator()
    }

    override fun vibrate(intensity: Float) {
        val vibration = PhoenixVibrator.getVibrationEffect(intensity) ?: return
        vibrator.vibrate(vibration)
    }
}

package com.example.flutter_usb_event

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.annotation.NonNull
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

class FlutterUsbEventPlugin : FlutterPlugin, MethodChannel.MethodCallHandler {

  private lateinit var context: Context
  private lateinit var channel: MethodChannel
  private var receiver: BroadcastReceiver? = null
  private val mainHandler = Handler(Looper.getMainLooper())
  private var isEngineAttached: Boolean = false

  override fun onAttachedToEngine(@NonNull binding: FlutterPlugin.FlutterPluginBinding) {
    context = binding.applicationContext
    channel = MethodChannel(binding.binaryMessenger, "flutter_usb_event")
    channel.setMethodCallHandler(this)
    isEngineAttached = true
    Log.d("FlutterUsbEvent", "Attached to Flutter engine.")
  }

  override fun onDetachedFromEngine(@NonNull binding: FlutterPlugin.FlutterPluginBinding) {
    stopListening()
    isEngineAttached = false
    Log.d("FlutterUsbEvent", "Detached from Flutter engine.")
  }

  override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
    when (call.method) {
      "startListening" -> {
        startListening()
        result.success(null)
      }
      "stopListening" -> {
        stopListening()
        result.success(null)
      }
      else -> result.notImplemented()
    }
  }

  private fun startListening() {
    if (receiver != null) {
      Log.d("FlutterUsbEvent", "Already listening for USB events.")
      return
    }

    val filter = IntentFilter().apply {
      addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
      addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
    }

    receiver = object : BroadcastReceiver() {
      override fun onReceive(context: Context, intent: Intent) {
        val device = intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)
        val name = device?.deviceName ?: "unknown"

        when (intent.action) {
          UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
            Log.d("FlutterUsbEvent", "USB device attached: $name")
            mainHandler.post {
              if (isEngineAttached) {
                channel.invokeMethod("onDeviceConnected", name)
              } else {
                Log.w("FlutterUsbEvent", "Engine not attached. Skipping onDeviceConnected.")
              }
            }
          }
          UsbManager.ACTION_USB_DEVICE_DETACHED -> {
            Log.d("FlutterUsbEvent", "USB device detached: $name")
            mainHandler.post {
              if (isEngineAttached) {
                channel.invokeMethod("onDeviceDisconnected", name)
              } else {
                Log.w("FlutterUsbEvent", "Engine not attached. Skipping onDeviceDisconnected.")
              }
            }
          }
        }
      }
    }

    context.registerReceiver(receiver, filter)
    Log.d("FlutterUsbEvent", "USB broadcast receiver registered.")
  }

  private fun stopListening() {
    if (receiver != null) {
      context.unregisterReceiver(receiver)
      receiver = null
      Log.d("FlutterUsbEvent", "USB broadcast receiver unregistered.")
    }
  }
}

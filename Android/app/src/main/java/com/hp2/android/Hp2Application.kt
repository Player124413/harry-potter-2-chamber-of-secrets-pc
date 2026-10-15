package com.hp2.android

import android.app.Application

class Hp2Application : Application() {
    override fun onCreate() {
        super.onCreate()
        TouchConfig.load(this)
    }
}

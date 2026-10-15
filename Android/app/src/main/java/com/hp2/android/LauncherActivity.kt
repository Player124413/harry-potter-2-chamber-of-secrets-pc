package com.hp2.android

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import com.hp2.android.databinding.ActivityLauncherBinding

class LauncherActivity : AppCompatActivity() {

    private lateinit var binding: ActivityLauncherBinding

    private val importPicker = registerForActivityResult(
        ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        if (uri == null) return@registerForActivityResult
        try {
            contentResolver.takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
        } catch (_: Exception) { }
        binding.status.text = getString(R.string.status_importing)
        // Persist the chosen tree URI; the game activity reads it on launch.
        getSharedPreferences("hp2", MODE_PRIVATE).edit()
            .putString("data_tree_uri", uri.toString()).apply()
        binding.status.text = getString(R.string.status_ready)
        Toast.makeText(this, "Game data linked.", Toast.LENGTH_SHORT).show()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityLauncherBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.btnNewGame.setOnClickListener { launchGame(newGame = true) }
        binding.btnContinue.setOnClickListener { launchGame(newGame = false) }
        binding.btnImport.setOnClickListener { importPicker.launch(null) }
    }

    private fun launchGame(newGame: Boolean) {
        val intent = Intent(this, GameActivity::class.java)
        intent.putExtra("new_game", newGame)
        startActivity(intent)
    }
}

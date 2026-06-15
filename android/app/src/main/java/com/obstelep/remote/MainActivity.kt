package com.obstelep.remote

import android.app.Activity
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.ScrollView
import android.widget.TextView
import org.json.JSONObject
import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.Executors

class MainActivity : Activity() {
    private val executor = Executors.newSingleThreadExecutor()
    private val handler = Handler(Looper.getMainLooper())
    private val connectionLock = Object()
    private var remoteSocket: Socket? = null
    private var remoteReader: BufferedReader? = null
    private var remoteWriter: BufferedWriter? = null
    @Volatile private var pendingJog: Double? = null
    @Volatile private var jogInFlight = false
    private val backgroundColor = Color.rgb(11, 15, 20)
    private val panelColor = Color.rgb(20, 27, 34)
    private val fieldColor = Color.rgb(28, 38, 48)
    private val textColor = Color.rgb(232, 238, 245)
    private val mutedTextColor = Color.rgb(148, 163, 184)
    private val accentColor = Color.rgb(34, 197, 94)
    private val accentPressedColor = Color.rgb(21, 128, 61)
    private val pausedColor = Color.rgb(220, 38, 38)
    private lateinit var host: EditText
    private lateinit var port: EditText
    private lateinit var token: EditText
    private lateinit var scriptUrl: EditText
    private lateinit var status: TextView
    private lateinit var discoveredList: LinearLayout
    private val playButtons = mutableListOf<Button>()
    private lateinit var controlTab: LinearLayout
    private lateinit var settingsTab: LinearLayout
    private lateinit var modeOne: LinearLayout
    private lateinit var modeTwo: LinearLayout
    private val statusPoller = object : Runnable {
        override fun run() {
            send("status", quiet = true)
            handler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.statusBarColor = backgroundColor
        window.navigationBarColor = backgroundColor
        @Suppress("DEPRECATION")
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        val prefs = getSharedPreferences("remote", MODE_PRIVATE)
        if (prefs.getBoolean("keepAwake", false)) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 32, 32, 32)
            setBackgroundColor(backgroundColor)
        }
        val scroll = ScrollView(this).apply {
            setBackgroundColor(backgroundColor)
            addView(root)
        }

        host = edit("OBS IP address", prefs.getString("host", "192.168.1.10") ?: "192.168.1.10")
        port = edit("Port", prefs.getString("port", "4457") ?: "4457")
        token = edit("Pairing token", prefs.getString("token", "") ?: "")
        scriptUrl = edit("Etherpad or plain text URL", prefs.getString("scriptUrl", "") ?: "")
        persist(host, "host")
        persist(port, "port")
        persist(token, "token")
        persist(scriptUrl, "scriptUrl")
        token.imeOptions = EditorInfo.IME_ACTION_DONE
        status = TextView(this).apply {
            text = "Disconnected"
            textSize = 18f
            setTextColor(mutedTextColor)
            setPadding(0, 24, 0, 24)
        }

        root.addView(status)

        controlTab = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        settingsTab = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
        }

        val tabRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val controlTabButton = button("Control", "controlTab")
        val settingsTabButton = button("Settings", "settingsTab")
        tabRow.addView(controlTabButton, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        tabRow.addView(settingsTabButton, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        root.addView(tabRow)

        val modeRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val modeOneButton = button("Mode 1", "mode1")
        val modeTwoButton = button("Mode 2", "mode2")
        modeRow.addView(modeOneButton, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        modeRow.addView(modeTwoButton, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        controlTab.addView(modeRow)

        modeOne = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        modeTwo = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
        }

        modeOne.addView(holdJogButton("↑", -5.0, dp(112)))
        modeOne.addView(bigPlayButton(dp(240)))
        modeOne.addView(holdJogButton("↓", 5.0, dp(112)))
        row(modeOne, "Speed -" to "speedDown", "Speed +" to "speedUp")

        modeTwo.addView(joystick())
        modeTwo.addView(playButtonSpacer())
        modeTwo.addView(bigPlayButton(dp(128)))
        controlTab.addView(modeOne)
        controlTab.addView(modeTwo)

        controlTabButton.setOnClickListener { setTab(0) }
        settingsTabButton.setOnClickListener { setTab(1) }
        modeOneButton.setOnClickListener { setMode(1) }
        modeTwoButton.setOnClickListener { setMode(2) }

        row(controlTab, "Stop" to "stop", "Restart" to "restart")
        row(controlTab, "Size -" to "sizeDown", "Size +" to "sizeUp")
        row(controlTab, "Prev Marker" to "previousMarker", "Next Marker" to "nextMarker")

        settingsTab.addView(host)
        settingsTab.addView(port)
        settingsTab.addView(token)
        settingsTab.addView(scriptUrl)
        settingsTab.addView(keepAwakeCheck(prefs.getBoolean("keepAwake", false)))
        row(settingsTab, "Scan Network" to "scan", "Load URL" to "loadUrl")
        discoveredList = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, 24, 0, 0)
        }
        settingsTab.addView(discoveredList)

        root.addView(controlTab)
        root.addView(settingsTab)

        setContentView(scroll)
        hideSystemUi()
        handler.postDelayed(statusPoller, 1000)
    }

    private fun hideSystemUi() {
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
    }

    private fun edit(hint: String, value: String): EditText =
        EditText(this).apply {
            this.hint = hint
            setText(value)
            setSingleLine(true)
            textSize = 16f
            setTextColor(textColor)
            setHintTextColor(mutedTextColor)
            backgroundTintList = ColorStateList.valueOf(fieldColor)
            setPadding(dp(12), 0, dp(12), 0)
        }

    private fun persist(editText: EditText, key: String) {
        editText.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                getSharedPreferences("remote", MODE_PRIVATE).edit().putString(key, s?.toString() ?: "").apply()
                closeRemoteConnection()
            }
            override fun afterTextChanged(s: Editable?) = Unit
        })
    }

    private fun row(root: LinearLayout, left: Pair<String, String>, right: Pair<String, String>) {
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row.addView(button(left.first, left.second), LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        row.addView(button(right.first, right.second), LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        root.addView(row)
    }

    private fun keepAwakeCheck(initial: Boolean): CheckBox =
        CheckBox(this).apply {
            text = "Always on screen"
            textSize = 15f
            setTextColor(textColor)
            isChecked = initial
            setOnCheckedChangeListener { _, checked ->
                getSharedPreferences("remote", MODE_PRIVATE).edit().putBoolean("keepAwake", checked).apply()
                if (checked) {
                    window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                } else {
                    window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                }
            }
        }

    private fun setMode(mode: Int) {
        modeOne.visibility = if (mode == 1) View.VISIBLE else View.GONE
        modeTwo.visibility = if (mode == 2) View.VISIBLE else View.GONE
        sendJog(0.0)
    }

    private fun setTab(tab: Int) {
        controlTab.visibility = if (tab == 0) View.VISIBLE else View.GONE
        settingsTab.visibility = if (tab == 1) View.VISIBLE else View.GONE
        sendJog(0.0)
    }

    private fun button(label: String, command: String): Button =
        Button(this).apply {
            text = label
            textSize = 15f
            setTextColor(textColor)
            background = rounded(panelColor, dp(10))
            minHeight = dp(52)
            setOnClickListener {
                if (command == "scan") scanNetwork() else send(command)
            }
        }

    private fun bigPlayButton(size: Int): Button =
        Button(this).apply {
            text = "Play/Pause"
            textSize = 26f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(Color.rgb(4, 12, 8))
            background = rounded(pausedColor, dp(18))
            minHeight = size
            minWidth = size
            setOnClickListener { send("playPause") }
            layoutParams = LinearLayout.LayoutParams(size, size).apply {
                gravity = Gravity.CENTER_HORIZONTAL
                setMargins(0, dp(12), 0, dp(18))
            }
            playButtons.add(this)
        }

    private fun holdJogButton(label: String, multiplier: Double, height: Int): Button =
        Button(this).apply {
            text = label
            textSize = 44f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(textColor)
            background = rounded(fieldColor, dp(18))
            minHeight = height
            setOnTouchListener { _, event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        sendJog(multiplier)
                        true
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                        sendJog(0.0)
                        true
                    }
                    else -> true
                }
            }
        }

    private fun joystick(): SeekBar =
        SeekBar(this).apply {
            max = 200
            progress = 100
            rotation = 270f
            scaleY = 2.8f
            layoutParams = LinearLayout.LayoutParams(dp(420), dp(180)).apply {
                gravity = Gravity.CENTER_HORIZONTAL
                setMargins(0, dp(96), 0, dp(96))
            }
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    if (!fromUser) return
                    val centered = progress - 100
                    val multiplier = if (kotlin.math.abs(centered) < 5) 0.0 else -centered / 20.0
                    sendJog(multiplier.coerceIn(-5.0, 5.0))
                }

                override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit

                override fun onStopTrackingTouch(seekBar: SeekBar?) {
                    seekBar?.progress = 100
                    sendJog(0.0)
                }
            })
        }

    private fun playButtonSpacer(): TextView =
        TextView(this).apply {
            text = "Joystick center = stop"
            textSize = 16f
            gravity = Gravity.CENTER_HORIZONTAL
            setTextColor(mutedTextColor)
        }

    private fun rounded(color: Int, radius: Int): GradientDrawable =
        GradientDrawable().apply {
            setColor(color)
            cornerRadius = radius.toFloat()
        }

    private fun dp(value: Int): Int =
        (value * resources.displayMetrics.density).toInt()

    private fun scanNetwork() {
        showStatus("Scanning for teleprompters...")
        runOnUiThread { discoveredList.removeAllViews() }
        executor.execute {
            val found = linkedMapOf<String, JSONObject>()
            try {
                DatagramSocket(null).use { socket ->
                    socket.reuseAddress = true
                    socket.broadcast = true
                    socket.soTimeout = 1800
                    socket.bind(InetSocketAddress(0))

                    val payload = JSONObject()
                        .put("type", "obs-telep-discover")
                        .put("version", 1)
                        .toString()
                        .toByteArray(Charsets.UTF_8)

                    broadcastAddresses().forEach { address ->
                        socket.send(DatagramPacket(payload, payload.size, address, 4458))
                    }

                    val deadline = System.currentTimeMillis() + 2200
                    val buffer = ByteArray(4096)
                    while (System.currentTimeMillis() < deadline) {
                        try {
                            val packet = DatagramPacket(buffer, buffer.size)
                            socket.receive(packet)
                            val json = JSONObject(String(packet.data, packet.offset, packet.length, Charsets.UTF_8))
                            if (json.optString("type") == "obs-telep") {
                                json.put("host", packet.address.hostAddress)
                                found["${packet.address.hostAddress}:${json.optInt("port", 4457)}"] = json
                            }
                        } catch (_: SocketTimeoutException) {
                            break
                        }
                    }
                }
                showScanResults(found.values.toList())
            } catch (ex: Exception) {
                showStatus("Scan failed: ${ex.message}")
            }
        }
    }

    private fun broadcastAddresses(): List<java.net.InetAddress> {
        val addresses = mutableListOf(java.net.InetAddress.getByName("255.255.255.255"))
        NetworkInterface.getNetworkInterfaces()?.toList()?.forEach { network ->
            if (!network.isUp || network.isLoopback) return@forEach
            network.interfaceAddresses.forEach { address ->
                val broadcast = address.broadcast
                if (broadcast is Inet4Address) addresses.add(broadcast)
            }
        }
        return addresses.distinctBy { it.hostAddress }
    }

    private fun showScanResults(results: List<JSONObject>) {
        runOnUiThread {
            discoveredList.removeAllViews()
            if (results.isEmpty()) {
                showStatus("No teleprompters found")
                return@runOnUiThread
            }
            showStatus("Found ${results.size} teleprompter(s)")
            results.forEach { result ->
                val resultHost = result.optString("host")
                val resultPort = result.optInt("port", 4457).toString()
                val title = result.optString("title", "Untitled")
                val name = result.optString("name", "OBS TeleP")
                val button = Button(this).apply {
                    text = "$name - $title\n$resultHost:$resultPort"
                    textSize = 14f
                    setTextColor(textColor)
                    background = rounded(fieldColor, dp(10))
                    setOnClickListener {
                        host.setText(resultHost)
                        port.setText(resultPort)
                        showStatus("Selected $resultHost:$resultPort")
                    }
                }
                discoveredList.addView(button)
            }
        }
    }

    private fun send(command: String, quiet: Boolean = false) {
        val commandName = when (command) {
            "speedUp", "speedDown" -> "speedDelta"
            "sizeUp", "sizeDown" -> "fontSizeDelta"
            "jogUp", "jogDown", "jogStop", "jog" -> "jog"
            else -> command
        }
        val value = when (command) {
            "speedUp" -> 10
            "speedDown" -> -10
            "sizeUp" -> 4
            "sizeDown" -> -4
            "jogUp" -> 5.0
            "jogDown" -> -5.0
            "jogStop" -> 0.0
            else -> null
        }
        val payload = JSONObject()
            .put("token", token.text.toString().trim())
            .put("command", commandName)
        if (command == "loadUrl") {
            payload.put("url", scriptUrl.text.toString().trim())
        }
        if (value != null) payload.put("value", value)

        executor.execute {
            try {
                val response = transact(payload)
                val playing = response.optBoolean("playing")
                val state = if (playing) "Playing" else "Paused"
                val progress = (response.optDouble("progress") * 100.0).toInt()
                val speed = response.optDouble("speed").toInt()
                val size = response.optInt("fontSize", 0)
                val sizeText = if (size > 0) " | ${size}px" else ""
                updatePlayButton(playing)
                showStatus("${response.optString("title", "Untitled")} | $state | $speed px/s$sizeText | $progress%")
            } catch (ex: Exception) {
                closeRemoteConnection()
                if (!quiet) showStatus("Connection failed: ${ex.message}")
            }
        }
    }

    private fun sendJog(multiplier: Double) {
        pendingJog = multiplier
        if (jogInFlight) return
        jogInFlight = true
        executor.execute {
            try {
                while (true) {
                    val next = pendingJog ?: break
                    pendingJog = null
                    val payload = JSONObject()
                        .put("token", token.text.toString().trim())
                        .put("command", "jog")
                        .put("value", next)
                    val response = transact(payload)
                    updatePlayButton(response.optBoolean("playing"))
                }
            } catch (_: Exception) {
                closeRemoteConnection()
            } finally {
                jogInFlight = false
                if (pendingJog != null) sendJog(pendingJog ?: 0.0)
            }
        }
    }

    private fun transact(payload: JSONObject): JSONObject {
        synchronized(connectionLock) {
            try {
                return transactLocked(payload)
            } catch (ex: Exception) {
                closeRemoteConnectionLocked()
                return transactLocked(payload)
            }
        }
    }

    private fun transactLocked(payload: JSONObject): JSONObject {
        ensureRemoteConnectionLocked()
        val writer = remoteWriter ?: throw IllegalStateException("No writer")
        val reader = remoteReader ?: throw IllegalStateException("No reader")
        writer.write(payload.toString())
        writer.write("\n")
        writer.flush()
        val line = reader.readLine() ?: throw IllegalStateException("Connection closed")
        return JSONObject(line)
    }

    private fun ensureRemoteConnectionLocked() {
        val current = remoteSocket
        if (current != null && current.isConnected && !current.isClosed) return

        val socket = Socket()
        socket.tcpNoDelay = true
        socket.keepAlive = true
        socket.soTimeout = 2500
        socket.connect(InetSocketAddress(host.text.toString().trim(), port.text.toString().toInt()), 900)
        remoteSocket = socket
        remoteWriter = BufferedWriter(OutputStreamWriter(socket.getOutputStream(), Charsets.UTF_8))
        remoteReader = BufferedReader(InputStreamReader(socket.getInputStream(), Charsets.UTF_8))
        remoteReader?.readLine()
    }

    private fun closeRemoteConnection() {
        synchronized(connectionLock) {
            closeRemoteConnectionLocked()
        }
    }

    private fun closeRemoteConnectionLocked() {
        try {
            remoteReader?.close()
        } catch (_: Exception) {
        }
        try {
            remoteWriter?.close()
        } catch (_: Exception) {
        }
        try {
            remoteSocket?.close()
        } catch (_: Exception) {
        }
        remoteReader = null
        remoteWriter = null
        remoteSocket = null
    }

    private fun updatePlayButton(playing: Boolean) {
        runOnUiThread {
            playButtons.forEach { button ->
                button.background = rounded(if (playing) accentColor else pausedColor, dp(18))
                button.setTextColor(if (playing) Color.rgb(4, 12, 8) else Color.WHITE)
                button.text = if (playing) "Pause" else "Play"
            }
        }
    }

    private fun showStatus(text: String) {
        runOnUiThread { status.text = text }
    }

    override fun onDestroy() {
        handler.removeCallbacks(statusPoller)
        closeRemoteConnection()
        executor.shutdownNow()
        super.onDestroy()
    }
}

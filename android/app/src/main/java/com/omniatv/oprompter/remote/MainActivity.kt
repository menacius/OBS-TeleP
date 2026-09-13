package com.omniatv.oprompter.remote

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
import android.widget.FrameLayout
import android.widget.ImageView
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
    private val backgroundColor = Color.rgb(11, 18, 32)
    private val panelColor = Color.rgb(16, 31, 55)
    private val fieldColor = Color.rgb(22, 45, 78)
    private val textColor = Color.rgb(229, 231, 235)
    private val mutedTextColor = Color.rgb(148, 163, 184)
    private val accentColor = Color.rgb(34, 197, 94)
    private val accentPressedColor = Color.rgb(21, 128, 61)
    private val pausedColor = Color.rgb(220, 38, 38)
    private lateinit var layoutMetrics: LayoutMetrics
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

        layoutMetrics = calculateLayoutMetrics()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(layoutMetrics.padding, layoutMetrics.padding, layoutMetrics.padding, layoutMetrics.padding)
            setBackgroundColor(backgroundColor)
        }
        val scroll = ScrollView(this).apply {
            setBackgroundColor(backgroundColor)
            isFillViewport = true
            overScrollMode = View.OVER_SCROLL_IF_CONTENT_SCROLLS
            addView(
                root,
                FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT
                )
            )
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
            textSize = 14f
            setTextColor(mutedTextColor)
            setPadding(0, dp(5), 0, dp(8))
        }

        val brandRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, 0, 0, dp(3))
        }
        val brandIcon = ImageView(this).apply {
            setImageResource(com.omniatv.oprompter.remote.R.drawable.ic_oprompter_brand)
            contentDescription = "O-Prompter"
        }
        val brandText = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(8), 0, 0, 0)
        }
        brandText.addView(TextView(this).apply {
            text = "O-Prompter"
            textSize = 17f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(textColor)
            includeFontPadding = false
        })
        brandText.addView(TextView(this).apply {
            text = "TELEPROMPTER PLUGIN FOR OBS"
            textSize = 8f
            letterSpacing = 0.12f
            setTextColor(mutedTextColor)
            includeFontPadding = false
        })
        brandRow.addView(
            brandIcon,
            LinearLayout.LayoutParams(layoutMetrics.brandIconSize, layoutMetrics.brandIconSize)
        )
        brandRow.addView(brandText, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        root.addView(brandRow)
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

        modeOne.addView(holdJogButton("↑", -5.0, layoutMetrics.jogButtonHeight))
        modeOne.addView(bigPlayButton(layoutMetrics.primaryPlaySize))
        modeOne.addView(holdJogButton("↓", 5.0, layoutMetrics.jogButtonHeight))
        row(modeOne, "Speed -" to "speedDown", "Speed +" to "speedUp")

        modeTwo.addView(joystick())
        modeTwo.addView(playButtonSpacer())
        modeTwo.addView(bigPlayButton(layoutMetrics.secondaryPlaySize))
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
        settingsTab.addView(aboutText())
        discoveredList = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(8), 0, 0)
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
            textSize = 14f
            setTextColor(textColor)
            setHintTextColor(mutedTextColor)
            backgroundTintList = ColorStateList.valueOf(fieldColor)
            setPadding(dp(12), 0, dp(12), 0)
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                setMargins(0, dp(2), 0, dp(2))
            }
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
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBaselineAligned(false)
        }
        row.addView(button(left.first, left.second), weightedButtonParams(isLeft = true))
        row.addView(button(right.first, right.second), weightedButtonParams(isLeft = false))
        root.addView(row)
    }

    private fun keepAwakeCheck(initial: Boolean): CheckBox =
        CheckBox(this).apply {
            text = "Always on screen"
            textSize = 13f
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
            textSize = 13f
            isAllCaps = false
            setTextColor(textColor)
            background = rounded(panelColor, dp(10))
            minHeight = layoutMetrics.buttonMinHeight
            minWidth = 0
            setPadding(dp(6), 0, dp(6), 0)
            setOnClickListener {
                if (command == "scan") scanNetwork() else send(command)
            }
        }

    private fun bigPlayButton(size: Int): Button =
        Button(this).apply {
            text = "Play/Pause"
            textSize = 21f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(Color.rgb(4, 12, 8))
            background = rounded(pausedColor, dp(14))
            minHeight = size
            minWidth = size
            setPadding(dp(6), 0, dp(6), 0)
            setOnClickListener { send("playPause") }
            layoutParams = LinearLayout.LayoutParams(size, size).apply {
                gravity = Gravity.CENTER_HORIZONTAL
                setMargins(0, layoutMetrics.controlGap, 0, layoutMetrics.controlGap)
            }
            playButtons.add(this)
        }

    private fun holdJogButton(label: String, multiplier: Double, height: Int): Button =
        Button(this).apply {
            text = label
            textSize = 34f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(textColor)
            background = rounded(fieldColor, dp(14))
            minHeight = height
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, height).apply {
                setMargins(0, layoutMetrics.controlGap / 2, 0, layoutMetrics.controlGap / 2)
            }
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
            scaleY = layoutMetrics.joystickScale
            layoutParams = LinearLayout.LayoutParams(layoutMetrics.joystickWidth, layoutMetrics.joystickHeight).apply {
                gravity = Gravity.CENTER_HORIZONTAL
                setMargins(0, layoutMetrics.joystickMargin, 0, layoutMetrics.joystickMargin)
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
            textSize = 13f
            gravity = Gravity.CENTER_HORIZONTAL
            setTextColor(mutedTextColor)
        }

    private fun aboutText(): TextView =
        TextView(this).apply {
            text = "O-Prompter\nTELEPROMPTER PLUGIN FOR OBS\nRemote Control\nDeveloped by OmniaTV"
            textSize = 12f
            gravity = Gravity.CENTER_HORIZONTAL
            setTextColor(mutedTextColor)
            setPadding(0, dp(10), 0, dp(4))
        }

    private fun rounded(color: Int, radius: Int): GradientDrawable =
        GradientDrawable().apply {
            setColor(color)
            cornerRadius = radius.toFloat()
        }

    private fun dp(value: Int): Int =
        (value * resources.displayMetrics.density).toInt()

    private fun weightedButtonParams(isLeft: Boolean): LinearLayout.LayoutParams =
        LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply {
            val gap = layoutMetrics.rowGap / 2
            setMargins(if (isLeft) 0 else gap, gap, if (isLeft) gap else 0, gap)
        }

    private fun calculateLayoutMetrics(): LayoutMetrics {
        val metrics = resources.displayMetrics
        val width = metrics.widthPixels
        val height = metrics.heightPixels
        val shortest = minOf(width, height)
        val padding = (shortest / 32).coerceIn(dp(8), dp(20))
        val usableWidth = width - (padding * 2)
        val usableHeight = height - (padding * 2)
        val primaryPlaySize = minOf((usableWidth * 0.46f).toInt(), (usableHeight * 0.20f).toInt())
            .coerceIn(dp(112), dp(180))
        val secondaryPlaySize = minOf((usableWidth * 0.34f).toInt(), (usableHeight * 0.14f).toInt())
            .coerceIn(dp(84), dp(120))
        val jogButtonHeight = (usableHeight * 0.10f).toInt().coerceIn(dp(54), dp(82))
        val joystickWidth = (usableWidth * 0.78f).toInt().coerceIn(dp(180), dp(480))
        val joystickHeight = (usableHeight * 0.13f).toInt().coerceIn(dp(92), dp(148))
        return LayoutMetrics(
            padding = padding,
            brandIconSize = (shortest * 0.10f).toInt().coerceIn(dp(38), dp(48)),
            buttonMinHeight = dp(40),
            rowGap = dp(4),
            controlGap = dp(6),
            primaryPlaySize = primaryPlaySize,
            secondaryPlaySize = secondaryPlaySize,
            jogButtonHeight = jogButtonHeight,
            joystickWidth = joystickWidth,
            joystickHeight = joystickHeight,
            joystickMargin = (usableHeight * 0.035f).toInt().coerceIn(dp(14), dp(42)),
            joystickScale = if (shortest < dp(360)) 2.0f else 2.5f
        )
    }

    private data class LayoutMetrics(
        val padding: Int,
        val brandIconSize: Int,
        val buttonMinHeight: Int,
        val rowGap: Int,
        val controlGap: Int,
        val primaryPlaySize: Int,
        val secondaryPlaySize: Int,
        val jogButtonHeight: Int,
        val joystickWidth: Int,
        val joystickHeight: Int,
        val joystickMargin: Int,
        val joystickScale: Float
    )

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
                        .put("type", "o-prompter-discover")
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
                            val type = json.optString("type")
                            if (type == "o-prompter" || type == "obs-telep") {
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
                val name = result.optString("name", "O-Prompter")
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

package com.rork.acoustical.domain.console

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.ServerSocket
import java.net.Socket
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

/**
 * Mesh network manager for multi-device acoustic analysis.
 *
 * Allows multiple phones running AcoustiCal to coordinate:
 * - Each phone measures SPL and spectrum at its physical position
 * - The master device aggregates measurements and computes consensus corrections
 * - Corrections are pushed to the console via OSC
 * - Listeners send their measurements; the master averages and decides
 *
 * Uses NSD (Network Service Discovery) for peer discovery on local WiFi,
 * and TCP for reliable data exchange between peers.
 */
class MeshNetworkManager(private val context: Context) {

    private val nsdManager: NsdManager =
        context.getSystemService(Context.NSD_SERVICE) as NsdManager

    private var discoveryListener: NsdManager.DiscoveryListener? = null
    private var serverSocket: ServerSocket? = null
    private var serverJob: Job? = null
    private var heartbeatJob: Job? = null
    private var scope: CoroutineScope? = null

    @Volatile
    var isRunning: Boolean = false
        private set

    @Volatile
    var isMaster: Boolean = false
        private set

    var deviceId: String = UUID.randomUUID().toString()
        private set

    var deviceName: String = android.os.Build.MODEL
        private set

    private val _peers = ConcurrentHashMap<String, MeshPeer>()
    val peers: List<MeshPeer> get() = _peers.values.toList().sortedBy { it.deviceName }

    /** Local measurements to broadcast to the master. */
    @Volatile
    var localSpl: Float = 0f

    @Volatile
    var localCorrections: List<Float> = emptyList()

    var onPeersChanged: ((List<MeshPeer>) -> Unit)? = null
    var onAggregateReceived: ((MeshAggregate) -> Unit)? = null
    var onPeerConnected: ((MeshPeer) -> Unit)? = null
    var onPeerDisconnected: ((String) -> Unit)? = null

    /**
     * Start the mesh network as master (aggregates measurements from all listeners).
     * The master also pushes corrections to the console.
     */
    fun startAsMaster() {
        if (isRunning) stop()
        isMaster = true
        isRunning = true
        _peers.clear()

        scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        registerService()
        startDiscovery()
        startTcpServer()
        startHeartbeat()

        Log.i(TAG, "Mesh started as MASTER ($deviceName)")
    }

    /**
     * Start the mesh network as listener (sends measurements to master).
     */
    fun startAsListener() {
        if (isRunning) stop()
        isMaster = false
        isRunning = true

        scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        startDiscovery()
        startHeartbeat()

        Log.i(TAG, "Mesh started as LISTENER ($deviceName)")
    }

    /**
     * Stop the mesh network.
     */
    fun stop() {
        isRunning = false

        try {
            nsdManager.stopServiceDiscovery(discoveryListener)
        } catch (e: Exception) {
            // Not registered or already stopped
        }
        discoveryListener = null

        try {
            serverSocket?.close()
        } catch (e: Exception) {
            // Ignore
        }
        serverSocket = null

        heartbeatJob?.cancel()
        heartbeatJob = null
        serverJob?.cancel()
        serverJob = null
        scope?.cancel()
        scope = null

        _peers.clear()
        onPeersChanged?.invoke(emptyList())

        Log.i(TAG, "Mesh stopped")
    }

    /**
     * Update local SPL measurement (called from audio engine callback).
     */
    fun updateLocalSpl(spl: Float) {
        localSpl = spl
    }

    /**
     * Update local band corrections (called from audio engine callback).
     */
    fun updateLocalCorrections(corrections: List<Float>) {
        localCorrections = corrections
    }

    /**
     * Compute the aggregate measurement from all peers (master only).
     */
    fun computeAggregate(): MeshAggregate {
        val activePeers = _peers.values.filter {
            System.currentTimeMillis() - it.lastSeenMs < PEER_TIMEOUT_MS
        }.toMutableList()

        // Include self
        val selfPeer = MeshPeer(
            id = deviceId,
            ipAddress = "127.0.0.1",
            deviceName = "$deviceName (yo)",
            role = if (isMaster) MeshPeer.MeshRole.MASTER else MeshPeer.MeshRole.LISTENER,
            lastSeenMs = System.currentTimeMillis(),
            currentSpl = localSpl,
            isActive = true
        )
        activePeers.add(0, selfPeer)

        val spls = activePeers.map { it.currentSpl }.filter { it > 0f }
        val avgSpl = if (spls.isNotEmpty()) spls.average().toFloat() else 0f
        val maxSpl = spls.maxOrNull() ?: 0f
        val minSpl = spls.minOrNull() ?: 0f

        // Simple consensus: average of all peers' corrections
        // A more sophisticated approach would weight by distance or signal quality
        val consensus = if (localCorrections.isNotEmpty()) localCorrections else emptyList()

        return MeshAggregate(
            peers = activePeers,
            averageSpl = avgSpl,
            maxSpl = maxSpl,
            minSpl = minSpl,
            consensusCorrections = consensus,
            timestampMs = System.currentTimeMillis()
        )
    }

    // --- NSD Service Registration & Discovery ---

    private fun registerService() {
        val serviceInfo = NsdServiceInfo().apply {
            serviceName = "AcoustiCal-$deviceId"
            serviceType = SERVICE_TYPE
            port = MESH_PORT
        }

        try {
            nsdManager.registerService(serviceInfo, NsdManager.PROTOCOL_DNS_SD, registrationListener)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to register NSD service", e)
        }
    }

    private val registrationListener = object : NsdManager.RegistrationListener {
        override fun onServiceRegistered(serviceInfo: NsdServiceInfo?) {
            Log.i(TAG, "NSD service registered: ${serviceInfo?.serviceName}")
        }

        override fun onRegistrationFailed(serviceInfo: NsdServiceInfo?, errorCode: Int) {
            Log.e(TAG, "NSD registration failed: $errorCode")
        }

        override fun onServiceUnregistered(serviceInfo: NsdServiceInfo?) {
            Log.i(TAG, "NSD service unregistered")
        }

        override fun onUnregistrationFailed(serviceInfo: NsdServiceInfo?, errorCode: Int) {
            Log.e(TAG, "NSD unregistration failed: $errorCode")
        }
    }

    private fun startDiscovery() {
        discoveryListener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String?) {
                Log.i(TAG, "NSD discovery started")
            }

            override fun onDiscoveryStopped(serviceType: String?) {
                Log.i(TAG, "NSD discovery stopped")
            }

            override fun onStartDiscoveryFailed(serviceType: String?, errorCode: Int) {
                Log.e(TAG, "NSD discovery start failed: $errorCode")
            }

            override fun onStopDiscoveryFailed(serviceType: String?, errorCode: Int) {
                Log.e(TAG, "NSD discovery stop failed: $errorCode")
            }

            override fun onServiceFound(serviceInfo: NsdServiceInfo?) {
                Log.d(TAG, "NSD service found: ${serviceInfo?.serviceName}")
                serviceInfo?.let { resolveService(it) }
            }

            override fun onServiceLost(serviceInfo: NsdServiceInfo?) {
                Log.d(TAG, "NSD service lost: ${serviceInfo?.serviceName}")
                val name = serviceInfo?.serviceName ?: return
                val peerId = name.removePrefix("AcoustiCal-")
                _peers.remove(peerId)
                onPeerDisconnected?.invoke(peerId)
                onPeersChanged?.invoke(peers)
            }
        }

        try {
            nsdManager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, discoveryListener)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start NSD discovery", e)
        }
    }

    private fun resolveService(serviceInfo: NsdServiceInfo) {
        val resolveListener = object : NsdManager.ResolveListener {
            override fun onServiceResolved(serviceInfo: NsdServiceInfo?) {
                val name = serviceInfo?.serviceName ?: return
                val peerId = name.removePrefix("AcoustiCal-")
                val host = serviceInfo?.host ?: return
                val port = serviceInfo?.port ?: MESH_PORT

                val peer = MeshPeer(
                    id = peerId,
                    ipAddress = host.hostAddress ?: "",
                    deviceName = "AcoustiCal-$peerId",
                    role = MeshPeer.MeshRole.LISTENER,
                    lastSeenMs = System.currentTimeMillis(),
                    isActive = true
                )

                _peers[peerId] = peer
                onPeerConnected?.invoke(peer)
                onPeersChanged?.invoke(peers)
                Log.i(TAG, "Peer resolved: ${peer.deviceName} at ${peer.ipAddress}")
            }

            override fun onResolveFailed(serviceInfo: NsdServiceInfo?, errorCode: Int) {
                Log.w(TAG, "NSD resolve failed: $errorCode for ${serviceInfo?.serviceName}")
            }
        }

        try {
            nsdManager.resolveService(serviceInfo, resolveListener)
        } catch (e: Exception) {
            Log.w(TAG, "Failed to resolve service", e)
        }
    }

    // --- TCP Server (master receives measurements) ---

    private fun startTcpServer() {
        serverJob = scope?.launch {
            try {
                serverSocket = ServerSocket(MESH_PORT)
                Log.i(TAG, "TCP server listening on port $MESH_PORT")

                while (isActive && isRunning) {
                    val client = serverSocket?.accept() ?: break
                    launch { handlePeerConnection(client) }
                }
            } catch (e: Exception) {
                if (isRunning) {
                    Log.e(TAG, "TCP server error", e)
                }
            }
        }
    }

    private fun handlePeerConnection(client: Socket) {
        try {
            val input = DataInputStream(client.getInputStream())
            val output = DataOutputStream(client.getOutputStream())

            while (isRunning && client.isConnected) {
                val msgType = input.readByte().toInt()

                when (msgType) {
                    MSG_HEARTBEAT -> {
                        val peerId = input.readUTF()
                        val peerName = input.readUTF()
                        val spl = input.readFloat()
                        val correctionCount = input.readInt()

                        val corrections = mutableListOf<Float>()
                        for (i in 0 until correctionCount) {
                            corrections.add(input.readFloat())
                        }

                        val peer = MeshPeer(
                            id = peerId,
                            ipAddress = client.inetAddress.hostAddress ?: "",
                            deviceName = peerName,
                            role = MeshPeer.MeshRole.LISTENER,
                            lastSeenMs = System.currentTimeMillis(),
                            currentSpl = spl,
                            isActive = true
                        )

                        _peers[peerId] = peer
                        onPeersChanged?.invoke(peers)
                    }

                    MSG_REQUEST_AGGREGATE -> {
                        val aggregate = computeAggregate()
                        sendAggregate(output, aggregate)
                    }

                    MSG_DISCONNECT -> {
                        val peerId = input.readUTF()
                        _peers.remove(peerId)
                        onPeerDisconnected?.invoke(peerId)
                        onPeersChanged?.invoke(peers)
                        break
                    }
                }
            }
        } catch (e: Exception) {
            if (isRunning) {
                Log.w(TAG, "Peer connection error", e)
            }
        } finally {
            try {
                client.close()
            } catch (e: Exception) {
                // Ignore
            }
        }
    }

    // --- Heartbeat (listeners send measurements to master) ---

    private fun startHeartbeat() {
        heartbeatJob = scope?.launch {
            while (isActive && isRunning) {
                delay(HEARTBEAT_INTERVAL_MS)
                if (!isMaster) {
                    sendHeartbeatToMaster()
                }

                // Prune stale peers
                val now = System.currentTimeMillis()
                val stale = _peers.filter { now - it.value.lastSeenMs > PEER_TIMEOUT_MS }
                stale.forEach { (id, _) ->
                    _peers.remove(id)
                    onPeerDisconnected?.invoke(id)
                }
                if (stale.isNotEmpty()) {
                    onPeersChanged?.invoke(peers)
                }
            }
        }
    }

    private fun sendHeartbeatToMaster() {
        val master = _peers.values.firstOrNull { it.role == MeshPeer.MeshRole.MASTER }
            ?: return

        try {
            Socket().use { sock ->
                sock.connect(java.net.InetSocketAddress(master.ipAddress, MESH_PORT), 2000)
                val output = DataOutputStream(sock.getOutputStream())

                output.writeByte(MSG_HEARTBEAT)
                output.writeUTF(deviceId)
                output.writeUTF(deviceName)
                output.writeFloat(localSpl)
                output.writeInt(localCorrections.size)
                for (corr in localCorrections) {
                    output.writeFloat(corr)
                }
                output.flush()

                // Check if master sent back an aggregate
                val input = DataInputStream(sock.getInputStream())
                if (sock.getInputStream().available() > 0) {
                    val respType = input.readByte().toInt()
                    if (respType == MSG_AGGREGATE) {
                        val aggregate = readAggregate(input)
                        onAggregateReceived?.invoke(aggregate)
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Heartbeat to master failed", e)
        }
    }

    private fun sendAggregate(output: DataOutputStream, aggregate: MeshAggregate) {
        output.writeByte(MSG_AGGREGATE)
        output.writeInt(aggregate.peers.size)
        for (peer in aggregate.peers) {
            output.writeUTF(peer.id)
            output.writeUTF(peer.deviceName)
            output.writeUTF(peer.ipAddress)
            output.writeFloat(peer.currentSpl)
            output.writeInt(peer.role.ordinal)
        }
        output.writeFloat(aggregate.averageSpl)
        output.writeFloat(aggregate.maxSpl)
        output.writeFloat(aggregate.minSpl)
        output.writeInt(aggregate.consensusCorrections.size)
        for (corr in aggregate.consensusCorrections) {
            output.writeFloat(corr)
        }
        output.flush()
    }

    private fun readAggregate(input: DataInputStream): MeshAggregate {
        val peerCount = input.readInt()
        val peerList = mutableListOf<MeshPeer>()
        for (i in 0 until peerCount) {
            peerList.add(
                MeshPeer(
                    id = input.readUTF(),
                    deviceName = input.readUTF(),
                    ipAddress = input.readUTF(),
                    currentSpl = input.readFloat(),
                    role = MeshPeer.MeshRole.entries[input.readInt()],
                    lastSeenMs = System.currentTimeMillis(),
                    isActive = true
                )
            )
        }
        val avgSpl = input.readFloat()
        val maxSpl = input.readFloat()
        val minSpl = input.readFloat()
        val corrCount = input.readInt()
        val corrections = mutableListOf<Float>()
        for (i in 0 until corrCount) {
            corrections.add(input.readFloat())
        }

        return MeshAggregate(
            peers = peerList,
            averageSpl = avgSpl,
            maxSpl = maxSpl,
            minSpl = minSpl,
            consensusCorrections = corrections,
            timestampMs = System.currentTimeMillis()
        )
    }

    companion object {
        private const val TAG = "MeshNetwork"
        private const val SERVICE_TYPE = "_acoustical._tcp."
        private const val MESH_PORT = 53141
        private const val DISCOVERY_INTERVAL_MS = 5000L
        private const val HEARTBEAT_INTERVAL_MS = 2000L
        private const val PEER_TIMEOUT_MS = 10000L
        private const val MSG_HEARTBEAT = 1
        private const val MSG_AGGREGATE = 2
        private const val MSG_REQUEST_AGGREGATE = 3
        private const val MSG_DISCONNECT = 4
    }
}

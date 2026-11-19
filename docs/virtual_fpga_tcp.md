# VaporView virtual FPGA TCP

Start the VirtualLab TCP server on IPv4 loopback, then start VaporView:

```powershell
.\build\Release\VaporView.exe --virtual-fpga
```

The default endpoint is `127.0.0.1:39181`. To select a different local port, add
`--virtual-fpga-port <port>`. Start a new VaporView process without these flags for
normal hardware operation. The FPGA page displays **Virtual FPGA TCP** and offers
Refresh and Connect if the server was unavailable at startup.

The server receives and sends raw VLP1 wire frames, including the CRC and the
existing zero padding to a four-byte boundary. TCP reads can contain a partial
frame or several frames. There is no extra envelope or socket handshake. The
startup sequence uses the existing PING, capability query, and register reads.
USB vendor control requests are unavailable through this transport.

Virtual mode pins the device route to the virtual FPGA and uses the usual FPGA
controls, telemetry and recording. Recordings use the `virtual_fpga_sessions`
subdirectory of the configured recording directory. Keep the VirtualLab listener
bound to `127.0.0.1`.

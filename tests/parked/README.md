# Parked tests

`test_manager.cpp` exercises the torrent path of `DownloadManager` (magnets,
.torrent imports, DHT bootstrap, peer state). That path is switched off in the
app, so these scenarios cannot pass until torrent support comes back. The file
is kept here, out of the build, so the engine tests can be revived with it.

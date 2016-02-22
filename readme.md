
##FTP UDP implementations

This repo includes two different variations of client and server, each tested within two different enviorments LAN and WAN.
Each subdirectory includes the test results.

Packet sizes are in compliance with unix MTU(less than 1500 bytes)



##Client types:
	goBackN:
		-Packets are either written or droped.
		-Packets that do not come in order are ignored.
		-saves time by not caching packets anywhere.
		-works best on local networks because, it is more reliable and the network are less likely to reorder or drop packets.
		-On a less reliable connection the client keeps fidgeting back, which makes it useless on a wide network.
		-large window sizes are impractical because packets can easily become stuck in congestion.

	selRepeat:
		-packets are always qued in memory.
		-a packet is placed in it`s order withing the que.
		-The Que is then emptied into stdout.
		-works best on less reliable connections, because it does not request packets unless it is necessary. Only droped packets are rerequested.
		-takes advantage of network delays by having a big window size in a WAN enviorment.
		-different window sizes in different enviorments.
 
##Usage:

The Following should be done on both the server-side and the client-side, if they are on different machines.
Go to a protocol type subdirectory, `cd goBackN` or `cd selRepeat`. If you are using this on a local area network `cd LAN` otherwise, `cd WAN`.  
  
`make`  
  
server-side:  
`./server <choose a port>`  
  
client-side:  
`./client <server`s IP address> <port> <filename>`  
  
##Experimentation
  
Files were generated with:  
	< /dev/urandom tr -dc "[:alnum:]" | head -c200000000 > test_file_server  
  
Files contents have been tested with:  
	cmp --silent onServer onClient || echo "files are different"  
  
enviorments:  
	FarEast Asia to Central-America:  
		-client running on a google cloud virtual machine in Aentral America.  
		-server is running on a google cloud virtual machine in far east Asia.  
	Local:
		-both server and client is running on the same network.  
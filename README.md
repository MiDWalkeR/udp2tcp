# UDP to TCP Data Forwarder

This program is designed to receive data blocks via UDP and forward them to a server via TCP. Each UDP packet can contain only one data block ranging from 16 to 128 bytes. The program appends four characters, which are provided as parameters, before the received data block and then redirects this block to the server through a TCP connection. The TCP connection with the server should be persistent. In case of connection loss, the program should automatically re-establish it. If UDP data arrives when the TCP connection is not established, the data packets should be discarded. Any data received from the TCP server should be ignored. During operation, the program logs important events such as establishing or terminating TCP connections, transmission errors, data reception, etc. The specific details of the logged information can be customized.

## How to Build 

To compile the program, just run

```bash
make clean && make
```

## How to Run.
```bash
./receiver <UDP IP:port> <TCP IP:port> <log_path> <first_char> <second_char> <third_char> <forth_char>
```

## Example:
```bash
./receiver 127.0.0.1:8080 127.0.0.1:8080 ./log a b c d
```

## How to check.
If you want you can modify makefile TARGET as 'server' and run 
```bash
make clean && make 
./server <IP> <PORT>
```
Please note as a server was not in a scope of the current task, it may be not working perfectly but for the testing purposes it would be enough.

Run receiver with a proper parameters which have been described previously. 

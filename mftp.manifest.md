# MFTP

## **Connection Establishment**

   - The client connects to the server by providing an IP address or domain name and a port number (default port is 6666).
   - The server and client exchange login credentials (username and password).
   - The server sends client address and port to data channel - like passive mode in FTP

## **Command & Data Channels**

   - FTP uses two channels: **command** (port 6666) and **data** (random port).
   - The **command channel** sends commands from the client (like "list files" or "download") and receives responses from the server.
   - The **data channel** is used to transfer files or directory listings.

## **Commands**

   - `LIST`: List files in the current directory.
   - `RETR <filename>`: Retrieve (download) a file from the server.
   - `STOR <filename>`: Store (upload) a file to the server. 
   - `DELE <filepath>`: Delete a file on the server.
   - `RMDR <dirpath>`: Remove directory.
   - `MKDR <dirname>`: Create a directory on the server.
   - `CHWD <dirpath>`: Change working directory.
   - `SIZE <filepath>`: Get size of file.
   - `USER <username>`: Provide authentication user.
   - `PASS <password>`: Provide authentication password.
   - `QUIT`: Disconnect.
   - `RNME <dirname:new-dirname>`: Rename item.
   - `NOOP`: Dummy packet.
   - `ABOR`: Abort transfer.
   - `MDTM <filepath>`: Get last modified datetime.
   - `FEAT`: List commands available on the server.
   - `PWRD`: Get current working directory.

## **File Transfer**

   - Files are transferred in binary mode only - no changes to file contents when reading or receiving.

## **Termination**

   - After the file operations are completed, the connection can be closed by the client.
   - The protocol relies on simple text-based commands, sent over TCP, to control file transfers between a client and a server.

## **command channel Message Format**

   - Messages are sent over plain-text ASCII, and are terminated with CRLF (`\r\n`)
   - Client messages begin with command, followed by, depending on command either CTLF or space and additional data.

		`<COMMAND>[ data]\r\n`

   - Server messages begin with either `AOK` or `ERR` to sigify if message is an error. This is followed by response code (described down bellow), that is then followed by additional information, then by custom message and then by CRLF.

		`<AOK/ERR> <CODE>[ data][ message]\r\n`

   - Sample message exchange

```txt
LIST\r\n
AOK 120 ([127.0.0.1]:45032) Opening data channel\r\n
AOK 320 Closing data channel\r\n
USER username\r\n
AOK 331 Password required for username\r\n
PASS wrong-password\r\n
ERR 530 Not logged in\r\n
```

## **Server response codes**

### Response code digits follow basic format:

  - 1xx - something is in progress, expect more messages;
  - 2xx - success message;
  - 3xx - information message - carries neither good, nor bad news;
  - 4xx - error - request was correct, but couldn't be completed;
  - 50x - error - request itself was malformed or not expected now;
  - 6xx - more information needed, expected more messages;
 
### And

  - x0x - general
  - x1x - file system
  - x2x - network connection / communication
  - x3x - authentication / policy

### Sample server codes:

  - `120` - Opening data channel
  - `200` - General success
  - `210` - File system action success (renaming, deletion, etc., was a success)
  - `220` - Ready
  - `230` - Logged in
  - `320` - Closing data channel (after ABOR or transfer completion)
  - `321` - Service closing (after QUIT)
  - `410` - Read failure (fwrite/fopen failed)
  - `411` - Write failure (fread/fopen failed)
  - `412` - Filesystem action failure (failed to rename, delete, etc.)
  - `420` - Data channel error (heheheha?, failed to open/read from/write to data channel)
  - `421` - Transfer aborted (when not prompted with ABOR)
  - `430` - Action forbidden (access denied, trying to cwd into "./../", etc)
  - `500` - Command doesn't exist
  - `501` - Command expected an argument, but none was provided
  - `502` - Argument to command was somehow invalid
  - `503` - Command isn't implemented on this server
  - `530` - Policy prevents anonymous login
  - `630` - Username OK, provide password

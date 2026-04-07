NexusLink - Decentralized LAN Cloud & Chat System

What is included
- NexusLink.exe
- data/downloads
- data/uploads
- data/logs

How to run on Windows
1) Open Command Prompt in this folder (dist).
2) Start server:
   NexusLink.exe --server 4040
3) On another laptop in the same LAN, run client:
   NexusLink.exe --client <SERVER_IP> 4040

Local same-machine test
- Terminal 1:
  NexusLink.exe --server 4040
- Terminal 2:
  NexusLink.exe --client 127.0.0.1 4040

GUI mode
- Start GUI:
  NexusLink.exe --gui

Chat usage
- Type messages and press Enter.
- Use /quit to disconnect.

File transfer usage
- Put files in data/uploads.
- Use chat command:
  /send <filename>
- Received files are saved in data/downloads.

Example IP usage
- If server laptop IP is 192.168.1.23:
  NexusLink.exe --client 192.168.1.23 4040

Notes
- Allow NexusLink.exe through Windows Firewall for Private networks.
- Keep both laptops on the same Wi-Fi/LAN.

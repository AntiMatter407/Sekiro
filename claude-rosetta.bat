@echo off
set NODE_EXTRA_CA_CERTS=C:\Users\guoya\AppData\Local\Temp\rosetta-ca-cert.pem
set NODE_TLS_REJECT_UNAUTHORIZED=0
"C:\Users\guoya\AppData\Local\Microsoft\WinGet\Packages\Anthropic.ClaudeCode_Microsoft.Winget.Source_8wekyb3d8bbwe\claude.exe" %*

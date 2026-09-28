# Security Policy / セキュリティポリシー

## 日本語

認証情報、デバイス識別情報、顧客データ、未修正の脆弱性を公開のIssueやpull requestへ投稿しないでください。

正式リリースに影響する脆弱性は、GitHubのPrivate vulnerability reportingまたはSecurity Advisoryから非公開で報告してください。リリースtag、プロファイル、ハードウェアrevision、再現手順、機密情報を除いたlogを添えてください。

標準製品は所有者によるESP32ファームウェアの改変を許可しています。Secure BootとFlash Encryptionは有効化していません。公式STM32更新packageは署名、対象、完全性の検証で保護され、検証用public keyは意図的に公開されています。

## English

Do not publish credentials, device identifiers, customer data, or an unpatched vulnerability in a public Issue or pull request.

Report vulnerabilities affecting an official release privately through GitHub Private vulnerability reporting or a Security Advisory. Include the release tag, profile, hardware revision, reproducible steps, and sanitized logs.

The standard product permits owner-controlled ESP32 firmware modification. Secure Boot and Flash Encryption are not enabled. Official STM32 update packages remain protected by signature, target, and integrity verification; the verification public key is intentionally public.

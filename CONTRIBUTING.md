# Local customization / ローカルでのカスタマイズ

## 日本語

このリポジトリはリリーススナップショットの配布専用であり、Issues、機能追加要望、pull requestを受け付けません。

MIT Licenseの範囲でリポジトリをcloneまたはforkし、ESP32ファームウェアを自由に改変できます。書込み前にDemo／Initialの両プロファイルをビルド可能な状態に保ち、次の公開検証を実行してください。

```bash
python3 scripts/verify_public_source.py --build
```

fork上の変更はforkの所有者が管理します。個別サポートや互換性保証の対象にはなりません。

## English

This repository is a release-snapshot distribution and does not accept Issues, feature requests, or pull requests.

You may clone or fork the repository and modify the ESP32 firmware under the MIT License. Keep both Demo and Initial profiles buildable and run the public verification before flashing:

```bash
python3 scripts/verify_public_source.py --build
```

Changes made in a fork are maintained by that fork's owner and are not covered by individual support or compatibility guarantees.

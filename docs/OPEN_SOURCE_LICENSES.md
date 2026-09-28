# Open-source licenses and source offer

## 日本語

ULSA EVO ESP32のプロジェクト部分はMIT Licenseで提供します。ただし、ビルドに含まれる第三者ソフトウェアには、それぞれのライセンス条件が適用されます。配布版には、次の2つを必ず同梱します。

- `THIRD_PARTY_NOTICES.md`：使用コンポーネント、版、ライセンス、参照元
- `ulsa-evo-esp32-license-bundle.tar.gz`：ライセンス本文、NOTICE、追加の帰属表示

`LICENSES/license-bundle-manifest.json`には、配布版が実際に参照したリンク済みアーカイブ、PlatformIOパッケージ情報、各ライセンス本文の取得元URLとSHA-256を記録します。標準ファームウェアを「MITのみ」として再配布しないでください。

公開スナップショットから検査用bundleを再生成するには、両profileをbuildした後で次を実行します。

```bash
python3 scripts/generate_compliance_artifacts.py --output-dir dist/compliance --require-resolved
```

Arduino-ESP32とAdafruit NeoPixelはLGPLで提供されます。これらを含むバイナリを再配布する場合は、同じRelease tagの公開ソースを対応するソースとして利用でき、同じビルド手順で変更後のオブジェクトを再リンクできます。変更したライブラリを使う場合は、変更内容と使用したソースを明記してください。

対応するソースをネットワークから取得できない場合のために、公開ソーススナップショットとライセンスバンドルの場所を配布物に明記します。書面によるソース提供が必要な場合は、配布物に記載された提供者窓口へ、対象Release tagを添えて依頼してください。

各ライセンス本文の著作権表示、NOTICE、免責事項、追加のリンク例外を削除・置換しないでください。ESP-IDFのように複数ライセンスを含むSDKは、バンドル内の`COPYRIGHT.rst`と個別本文を併せて確認してください。

## English

The ULSA EVO ESP32 project code is provided under the MIT License. Third-party software linked into a build remains subject to its own license terms. Every distributed build includes both:

- `THIRD_PARTY_NOTICES.md`: component, version, license, and source inventory
- `ulsa-evo-esp32-license-bundle.tar.gz`: full license texts, notices, and additional attributions

`LICENSES/license-bundle-manifest.json` records the actual linked archives, PlatformIO package evidence, and the source URL and SHA-256 for each retrieved license file. Do not redistribute the standard firmware as MIT-only software.

After building both profiles, regenerate the verification bundle with:

```bash
python3 scripts/generate_compliance_artifacts.py --output-dir dist/compliance --require-resolved
```

Arduino-ESP32 and Adafruit NeoPixel are LGPL-licensed. When redistributing a binary containing them, the public source snapshot at the matching Release tag is the corresponding source, and the documented build steps can relink replacement objects after a library change. Identify any modified library and the source used for the modified build.

The distribution identifies the public source snapshot and license bundle so that the corresponding source remains available even when a device is offline. If a written source offer is required, contact the provider named in the distribution and include the exact Release tag.

Retain every copyright notice, NOTICE file, disclaimer, and additional linking exception. SDKs such as ESP-IDF contain multiple licenses; review `COPYRIGHT.rst` and the individual texts in the bundle together.

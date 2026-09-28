# SD Recording / SDカード記録

## 日本語

Demoファームウェアは、アプリを閉じたりBLEを切断したりしても本体のSDカードへ記録を続けられます。InitialにはSD記録機能がありません。

### 開始・停止

- 本体: I2C計測中にボタンを1回押すと開始し、もう一度1回押すと停止します。2回はUART Bridge、3回はCommandの選択です。これらのモードではSD記録を開始できず、記録中はモード切替を拒否します。Bridge／Commandからは2秒長押しでI2Cへ戻れます。
- アプリ（Web／iOS）: 「ログ」で保存先「カード」を選択して「開始」。終了時は「停止」してください。「アプリ」だけを選んでもカード記録は始まりません。
- 自動開始: 設定の「内部ログを自動開始」を有効にすると、次回起動時にカードが使用できれば記録を開始します。既定値は無効です。この設定は現在の記録状態を変えず、手動停止しても残ります。
- USB: `sd stop`で停止、`sd stats`で状態を確認します。COMMANDモードでの`sd resume`は計測が休止するため拒否されます。開始はI2C計測中の本体ボタンまたはアプリを使ってください。`sd remount`は停止・再接続だけを行います。

停止処理が完了してからカードを取り外してください。カード処理に時間がかかる場合、停止要求後も保存待ちのデータを処理しています。

### 時刻とファイル

RTCはUTCを保持し、設定済みIANA地域のローカル暦で日付別フォルダーとFAT metadataを作ります。地域時刻へ変換できる場合、ファイル名はsegment開始時刻のローカル日時`YYYYMMDD_hhmmss.csv`です。保存地域を解決できない場合はUTC日時を使います。同じ開始秒のファイルが既にある場合は`_01`以降の番号を付け、既存ファイルへ上書き・追記しません。timezone未設定またはUTC未同期の場合は`boot_logs/<session>_<segment>.csv`へ保存し、RTCに仮の日時を設定しません。この場合だけランダムなセッションIDを使います。約30分または16 MiBに加え、地域、標準差、DST差、合計UTC差が変わった時点でも現在ファイルを閉じ、新しいsegmentへ切り替えます。

| CSV列 | 内容 |
|---|---|
| `timestamp_iso` | IANA地域で変換した日時と分単位UTC差（例`+05:45`）。日時不明時は`BOOT+経過ミリ秒` |
| `timestamp_epoch_ms` | RTCのUTCから直接生成したUnixミリ秒。日時不明時は空欄 |
| `uptime_ms` | 本体起動から受信までの経過ミリ秒。49.7日を越えて継続 |
| `source_seq` | センサーの連番。周回するため時刻には使いません |
| `record_index` | 起動後の保存対象連番。欠番は保存できなかった区間を示します |
| `dropped_total` | 起動後に保存できなかったレコードの累計 |
| `uncertain_total` | 書込み・同期異常で保存を確認できないレコードの累計 |

日時不明時のファイル更新日時は、FAT形式の制約により1980-01-01を未設定値として使います。これは計測日ではありません。計測日時の判断にはCSVを使ってください。

### 長期間の記録

容量が不足すると既存データを保持して停止します。古いログの自動削除はしません。通常は約1秒ごとに同期しますが、カードの遅延や電断時の損失が1秒以内とは限りません。

60日分を1行最大191 byteで見積もると、1 Hzで約0.99 GB、10 Hzで約9.90 GB、50 Hzで約49.51 GBです（10進GB、フォルダー等の余白は別途必要）。実際の行長によって減ります。使用中のカードの空き容量と保存周期を確認してください。この構成はexFATには対応していません。

保存待ちは256件までです。50 Hzでは約5.1秒、10 Hzでは約25.6秒分で、長い遅延では欠測が発生します。一過性の書込み障害は最大5回の自動復旧を試みます。容量不足や復旧失敗は停止として通知します。

アプリの「カードログ詳細」では、同期確認済み・欠測・保存未確認・保存待ちを確認できます。カードと設定周期に応じた長期検証を行ってから無人運用してください。

## English

Demo firmware can keep recording to the device SD card after the app closes or BLE disconnects. Initial does not provide SD recording.

### Start and stop

- Device: click once during I2C measurement to start, and once again to stop. Two clicks select UART Bridge and three select Command. Neither mode can start SD recording, and mode changes are rejected while recording. Hold for two seconds in Bridge or Command to return to I2C.
- App (Web/iOS): select “カード” (card) as a log destination, then start. Use stop to finish. Selecting only the app destination does not start card recording.
- Auto-start: enable “内部ログを自動開始” to start at the next boot if the card is available. It defaults to off, does not change the current recording state, and remains enabled after a manual stop.
- USB: use `sd stop` and `sd stats`. `sd resume` is rejected in COMMAND mode because measurement is paused; start recording from the button or app while in I2C measurement. `sd remount` stops and reconnects the card without starting.

Wait for stopping to finish before removing the card. Pending records may still be draining after a stop request.

### Timestamps and files

The RTC stores UTC. Valid UTC plus a configured IANA zone produces local date folders and FAT metadata. When local conversion is available, segments use their local start time as `YYYYMMDD_hhmmss.csv`; if a saved zone cannot be resolved, the UTC time is used. If that exact second already exists, a `_01` or later suffix is used; existing files are never overwritten or appended to. An unset zone or unsynchronized UTC uses `boot_logs/<session>_<segment>.csv` with a random session ID, because no trustworthy date is available; no substitute date is written to the RTC. Files rotate after approximately 30 minutes or 16 MiB, and whenever the zone, standard offset, DST offset, or total UTC offset changes.

| CSV column | Meaning |
|---|---|
| `timestamp_iso` | IANA-local timestamp with a minute-resolution offset (for example `+05:45`), or explicit `BOOT+elapsed milliseconds` when unknown |
| `timestamp_epoch_ms` | Unix milliseconds generated directly from RTC UTC; empty when unknown |
| `uptime_ms` | Time from device boot to sample reception; continues past 49.7 days |
| `source_seq` | Wrapping sensor sequence number, not a timestamp |
| `record_index` | Recording-candidate index since boot; gaps identify records that could not be saved |
| `dropped_total` | Records that could not be saved since boot |
| `uncertain_total` | Records whose storage could not be confirmed after a write/sync error |

For unknown timestamps, FAT file metadata uses 1980-01-01 as an unset value. This is not a measurement date. Use CSV timestamps to interpret measurement time.

### Extended recording

The logger preserves existing records and stops when space is insufficient. It does not delete old logs automatically. Normal synchronization runs approximately once per second, but card stalls and power loss can affect more than one second of data.

At a maximum of 191 bytes per row, 60 days requires approximately 0.99 GB at 1 Hz, 9.90 GB at 10 Hz, or 49.51 GB at 50 Hz (decimal GB, plus filesystem overhead). Actual row sizes may be smaller. Check free space and recording interval. This configuration does not support exFAT.

The queue holds up to 256 records: about 5.1 seconds at 50 Hz or 25.6 seconds at 10 Hz. Longer stalls cause gaps. Transient write faults permit up to five automatic recovery attempts; insufficient capacity and exhausted recovery stop recording.

The app card-log details show synchronized, dropped, uncertain, and queued records. Validate the intended card and recording interval before unattended operation.

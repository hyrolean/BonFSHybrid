BonDriver_FSHybrid.dll MOD @ 2021/12/7

■KEIAN/Digibest系3つの機能を同時に有するハイブリッド型BonDriver.dll

  ファイル名を変えることにより動作を変更可能
  ファイル名を適切に変更しないときちんと動作しないので注意

  BonDriver_FSHybrid.dll
  → BonDriver_FSUSB2N.dll / BonDriver_FSUSB2Nxxxxxx.dll ※

    FSUSB2新型(V2)、または、FSPCIE 用 BonDriver として動作

      取扱説明書 : readme_mod_FSUSB2N.txt


  BonDriver_FSHybrid.dll
  → BonDriver_FSUSB2i.dll / BonDriver_FSUSB2ixxxxxx.dll ※

    FSUSB2/V3、または、FSMINI 用 BonDriver として動作

      取扱説明書 : readme_mod_FSUSB2i.txt


  BonDriver_FSHybrid.dll
  → BonDriver_uSUNpTV.dll / BonDriver_uSUNpTVxxxxxx.dll ※

    US-3POUT (さんぱくん外出) 用 BonDriver として動作

      取扱説明書 : readme_mod_uSUNpTV.txt


  ※: xxxxxx は、任意の英数字


■更新履歴

2021/3/18 からの変更点

  ・iniファイルにチューナーID自動割り当て時にIDを循環させる以下の２項目を追加

     DEVICE_ID_ROTATION / DEVICE_ID_ROTATION_VOLATILE

2021/2/7 からの変更点

  ・各々のBonDriverをFastScan(高速チャンネルスキャン)に対応

2020/11/22 からの変更点

  ・iniファイルに以下の既定の三波チャンネル配置設定に関する以下の２項目を追加

     DEF_SPACE_BS_STREAM_STRIDE / DEF_SPACE_CS110_STREAM_STRIDE

2020/11/3 からの変更点

  ・iniファイルの TSTHREAD_NUMIO 項目に指定できる最大値を64から256に拡張[11/22]
  ・iniファイルにTS循環スレッド用マルチタスク設定項目 TSTHREAD_DUPLEX を追加
  ・iniファイルにUSBデバイスのアイドル時省電力機能を無効にするかどうかを決定す
    るための項目 USBPOWERPOLICY_AVOID_SUSPEND を追加

2020/11/1 からの変更点

  ・iniファイルに以下の項目群を新たに追加

    TSTHREAD_POLL_TIMEOUT / TSTHREAD_SUBMIT_TIMEOUT / TSTHREAD_SUBMIT_IOLIMIT /
	TSTHREAD_NUMIO / TSALLOC_TIMEOUT / TSALLOC_WAITING / TSALLOC_MODERATE /
	TUNER_RETRY_DURATION

2020/10/24 からの変更点

  ・iniファイルにTSスレッドの優先順位を変更することのできる以下の２項目を追加

     TSTHREAD_PRIORITY / TSALLOC_PRIORITY

2020/10/17 からの変更点

  ・iniファイルに書いた計算式が正しく機能しないことのあるバグを修正[10/24rev2]
  ・iniファイルの整数項目に計算式を書くことのできる機能を追加

2020/10/9 からの変更点

  ・アイソクロナス転送の負荷を多少改善
  ・iniファイルにNULLパケットをシャットアウトするかどうかを判断するためのフラグ
    TSCACHING_DROPNULLPACKETS 項目を追加

2020/6/20 からの変更点

  ・iniファイルにバルク転送のパケットサイズをオーバーライドすることのできる
    TSCACHING_BULKPACKETSIZE 項目を追加

  ・アイソクロナス転送に対応した以下のソリューションを新たに追加

     FSHybrid_isoch.sln / uSUNpTV_isoch.sln / FSUSB2N_isoch.sln

2020/4/27 からの変更点

  ・iniファイルに以下の三系統のシグナル取得時排他に関わるフラグを追加

     FSUSB2N_LOCK_ON_SIGNAL / FSUSB2I_LOCK_ON_SIGNAL / USUNPTV_LOCK_ON_SIGNAL

2020/1/12 からの変更点

  ・dBが良くてもなぜかとりこぼす謎のプチドロップ現象の根本的な問題を修正
   （WinUsbのパイプ読出し系統の処理を排他で行う仕組みのコードを追加して対応）

2020/1/3 からの変更点

  ・iniファイルから設定を読み込める機能を追加
    (サンプルとしてBonDriver_FSHybrid.iniを添付)

  ・デバイスの初期化に失敗した場合は既定で最大３回まで再試行する仕様に変更

2019/12/23 からの変更点

  ・コードをリファクタリングしてバイナリサイズを約10KB削減


■使用制限

  無保証（NO WARRANTY）



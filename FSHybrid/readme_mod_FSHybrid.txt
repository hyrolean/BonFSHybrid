BonDriver_FSHybrid.dll MOD @ 2020/10/14

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



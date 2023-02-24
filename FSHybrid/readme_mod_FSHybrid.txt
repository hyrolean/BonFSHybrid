BonDriver_FSHybrid.dll MOD @ 2020/1/12

■KEIAN/Digibest系3つの機能を同時に有するハイブリッド型BonDriver.dll

  ファイル名を変えることにより動作を変更可能
  ファイル名を適切に変更しないときちんと動作しないので注意

  BonDriver_FSHybrid.dll
  → BonDriver_FSUSB2N.dll / BonDriver_FSUSB2Nxxxxxx.dll ※

    FSUSB2新型(V2)、または、FSPCIE 用 BonDriver として動作

      取扱説明書 : readme_mod_FSUSB2N.txt


  BonDriver_FSHybrid.dll
  → BonDriver_FSUSB2i.dll / BonDriver_FSUSB2ixxxxxx.dll ※

    FSUSB2/V3 用 BonDriver として動作

      取扱説明書 : readme_mod_FSUSB2i.txt


  BonDriver_FSHybrid.dll
  → BonDriver_uSUNpTV.dll / BonDriver_uSUNpTVxxxxxx.dll ※

    US-3POUT (さんぱくん外出) 用 BonDriver として動作

      取扱説明書 : readme_mod_uSUNpTV.txt


  ※: xxxxxx は、任意の英数字


■更新履歴

2020/1/3 からの変更点

  ・iniファイルから設定を読み込める機能を追加
    (サンプルとしてBonDriver_FSHybrid.iniを添付)

  ・デバイスの初期化に失敗した場合は既定で最大３回まで再試行する仕様に変更

2019/12/23 からの変更点

  ・コードをリファクタリングしてバイナリサイズを約10KB削減


■使用制限

  無保証（NO WARRANTY）



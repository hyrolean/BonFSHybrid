BonDriver_FSUSB2N.dll MOD @ 2019/12/22

■オリジナル版から新たに追加した機能

  ・IDで個別指定できる機能を追加

    BonDriver_FSUSB2N_dev + ID(番号) + .dll のファイル名の記述方法で指定可能。
    BonDriver_FSUSB2N_dev0.dll ← 複数台接続した1番目の機器
    BonDriver_FSUSB2N_dev1.dll ← 複数台接続した2番目の機器
    BonDriver_FSUSB2N_dev2.dll ← 複数台接続した3番目の機器
    上記以外の記述の場合は、空いている機器から順に探すオリジナルに準拠。
    BonDriver_FSUSB2N_abc.dll などと記述するといままで通り自動でIDを割当てる。


  ・独自チャンネルファイル機能を追加

    ドライバと同じ名前で拡張子を .ch.txt にしたものにチャンネル情報を記述する
    ことにより独自のチャンネル情報にアレンジすることが可能。
    サンプルとして BonDriver_FSUSB2N.ch.txt を添付。


■更新履歴

2019/12/8 からの変更点

  ・キャッシュアロケータがドロップを引き起こすことのあるバグを修正[12/22]

  ・ソース統合により非同期バッファに関する数値を調整[12/21(ES)]

    パケットサイズの変更に伴い、非同期バッファに関する以下の規定値を微調整

      ASYNCTS_QUEUENUM … 初期バッファ数 規定値66  3M (47K*66) bytes
      ASYNCTS_QUEUEMAX … 最大バッファ数 規定値660 30M (47K*660) bytes
      ASYNCTS_EMPTYBORDER … アロケーション開始空バッファ下限数 規定値22 1MB
      ASYNCTS_EMPTYLIMIT … オーバーラップから保障するバッファ数 規定値11 0.5MB

  ・キャッシュアロケータのメモリアクセスエラー不具合を修正[12/21(ES)]

2019/12/7 からの変更点

  ・レジストリに非同期バッファに関するオプションを追加

    レジストリ HKEY_CURRENT_USER\Software\tri.dw.land.to\FSUSB2N 上に
    以下の４項目をDWORD値で変更可能。

      ASYNCTS_QUEUENUM … 初期バッファ数 規定値32  2M (64K*32) bytes
      ASYNCTS_QUEUEMAX … 最大バッファ数 規定値320 20M (64K*320) bytes
      ASYNCTS_EMPTYBORDER … アロケーション開始空バッファ下限数 規定値12 750KB
      ASYNCTS_EMPTYLIMIT … オーバーラップから保障するバッファ数 規定値6 375KB

2018/3/4 からの変更点

  ・レジストリにキャッシングに関するオプションを追加

    レジストリ HKEY_CURRENT_USER\Software\tri.dw.land.to\FSUSB2N 上に
    "TSCACHING_LEGACY"というDWORD値を作成して値として"1"を記述すると
    tri.dw.land.toさん直伝のキャッシュ方式で動作させることが可能。
    (※現行のキャッシュ方式に戻すには値自体を削除するか値として"0"を記述)

  ・FIFOバッファリング最適化

    稀に発生するドロップの修正。

2015/1/4 からの変更点

  ・開発環境を VisualStudio 2015 に変更
  ・IDで個別指定できる機能のファイル名の書式を
	 BonDriver_FSUSB2_ + 番号 + .dll から
	 BonDriver_FSUSB2N_dev + 番号 + .dll に変更

2015/1/3 からの変更点

  ・PurgeTSStream() がコールされると最悪TSストリームがストップするバグを修正


■使用制限

  無保証（NO WARRANTY）



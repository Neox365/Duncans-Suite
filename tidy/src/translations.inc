// translations.inc - Oberflaechentexte in allen Sprachen (wird von Download-Cleaner.cpp eingebunden).
// Schluessel ist der deutsche Text; danach die Uebersetzungen in der Reihenfolge von kLangs ab Index 1: en, fr, es, pl, pt, ja, zh.
// Platzhalter (%d, %s, %.0f, %%) und \n muessen in jeder Sprache erhalten bleiben; Leerzeichen am Anfang/Ende sind Teil des Satzbaus.
static const Translation kTranslations[] = {
    {L"Windows-Komponentenspeicher, Schattenkopien und Ereignisprotokolle – das kann bis zu einer halben Stunde dauern, Tidy bitte offen lassen …",
     {L"Windows component store, shadow copies and event logs – this can take up to half an hour, please keep Tidy open …", L"Magasin de composants Windows, clichés instantanés et journaux d'événements – cela peut durer jusqu'à une demi-heure, laissez Tidy ouvert …", L"Almacén de componentes de Windows, instantáneas y registros de eventos – puede tardar hasta media hora, deja Tidy abierto …", L"Magazyn składników systemu Windows, kopie w tle i dzienniki zdarzeń – może to potrwać do pół godziny, pozostaw Tidy otwarte …", L"Armazenamento de componentes do Windows, cópias de sombra e logs de eventos – pode levar até meia hora, mantenha o Tidy aberto …", L"Windows のコンポーネントストア、シャドウコピー、イベントログ（最大30分かかることがあります。Tidy を開いたままにしてください）…", L"Windows 元件存放區、陰影複製與事件記錄檔（最多可能需要半小時，請保持 Tidy 開啟）…"}},
    {L"Windows-Aktionen laufen …  %u:%02u Min.",
     {L"Windows actions running …  %u:%02u min", L"Actions Windows en cours …  %u:%02u min", L"Acciones de Windows en curso …  %u:%02u min", L"Akcje systemu Windows trwają …  %u:%02u min", L"Ações do Windows em andamento …  %u:%02u min", L"Windows の操作を実行中 …  %u:%02u 分", L"正在執行 Windows 操作 …  %u:%02u 分"}},
    // Ultra-Tiefenreinigung (1.2.1)
    {L"Ultra-Tiefenreinigung",
     {L"Ultra deep clean", L"Nettoyage ultra-approfondi", L"Limpieza ultra profunda", L"Czyszczenie ultra-głębokie", L"Limpeza ultraprofunda", L"ウルトラ・ディープクリーン", L"超深度清理"}},
    {L"App-Caches (Discord, Slack, Steam, VS Code …)",
     {L"App caches (Discord, Slack, Steam, VS Code …)", L"Caches d'applications (Discord, Slack, Steam, VS Code …)", L"Cachés de aplicaciones (Discord, Slack, Steam, VS Code …)", L"Pamięci podręczne aplikacji (Discord, Slack, Steam, VS Code …)", L"Caches de aplicativos (Discord, Slack, Steam, VS Code …)", L"アプリのキャッシュ（Discord、Spotify、VS Code、Teams）", L"應用程式快取（Discord、Spotify、VS Code、Teams）"}},
    {L"Microsoft-Store-Reste",
     {L"Microsoft Store leftovers", L"Restes du Microsoft Store", L"Restos de Microsoft Store", L"Pozostałości Microsoft Store", L"Restos da Microsoft Store", L"Microsoft Store の残り", L"Microsoft Store 殘留檔案"}},
    {L"Diagnose- und ETL-Protokolle",
     {L"Diagnostic and ETL logs", L"Journaux de diagnostic et ETL", L"Registros de diagnóstico y ETL", L"Dzienniki diagnostyczne i ETL", L"Logs de diagnóstico e ETL", L"診断ログと ETL ログ", L"診斷與 ETL 記錄"}},
    {L"Zwischenspeicher der Programme Discord, Spotify, VS Code und Teams – Anmeldung und Einstellungen bleiben, die Programme bauen den Cache neu auf",
     {L"Caches of Discord, Spotify, VS Code and Teams – sign-in and settings stay, the programs rebuild the cache", L"Caches de Discord, Spotify, VS Code et Teams – connexion et paramètres sont conservés, les programmes reconstruisent le cache", L"Cachés de Discord, Spotify, VS Code y Teams – el inicio de sesión y los ajustes se conservan, los programas reconstruyen la caché", L"Pamięci podręczne Discord, Spotify, VS Code i Teams – logowanie i ustawienia zostają, programy odbudowują pamięć podręczną", L"Caches de Discord, Spotify, VS Code e Teams – login e configurações permanecem, os programas recriam o cache", L"Discord、Spotify、VS Code、Teams のキャッシュ。サインインと設定は残り、キャッシュはアプリが再作成します", L"Discord、Spotify、VS Code 與 Teams 的快取，登入與設定會保留，程式會重新建立快取"}},
    {L"Zwischengespeicherte und vorübergehende Downloads des Microsoft Store",
     {L"Cached and temporary downloads of the Microsoft Store", L"Téléchargements en cache et temporaires du Microsoft Store", L"Descargas en caché y temporales de Microsoft Store", L"Pobrane w pamięci podręcznej i tymczasowe pliki Microsoft Store", L"Downloads em cache e temporários da Microsoft Store", L"Microsoft Store のキャッシュと一時ダウンロード", L"Microsoft Store 的快取與暫存下載"}},
    {L"Ablaufverfolgungs-Protokolle (.etl) von Windows. Von Windows benutzte Dateien bleiben (ohne Administratorrechte)",
     {L"Windows trace logs (.etl). Files in use by Windows stay (without administrator rights)", L"Journaux de suivi Windows (.etl). Les fichiers utilisés par Windows restent (sans droits administrateur)", L"Registros de seguimiento de Windows (.etl). Los archivos en uso por Windows se conservan (sin derechos de administrador)", L"Dzienniki śledzenia systemu Windows (.etl). Pliki używane przez system pozostają (bez uprawnień administratora)", L"Logs de rastreamento do Windows (.etl). Arquivos em uso pelo Windows permanecem (sem direitos de administrador)", L"Windows のトレースログ（.etl）。Windows が使用中のファイルは残ります（管理者権限がない場合）", L"Windows 追蹤記錄（.etl）。Windows 使用中的檔案會保留（無系統管理員權限時）"}},
    {L"Ultra: alles inklusive Shader-, Update-, Treiber-, App-Cache-, Store- und ETL-Reste",
     {L"Ultra: everything incl. shader, update, driver, app cache, Store and ETL leftovers", L"Ultra : tout, y compris restes de shaders, mises à jour, pilotes, caches d'applications, Store et ETL", L"Ultra: todo, incl. restos de shaders, actualizaciones, controladores, cachés de aplicaciones, Store y ETL", L"Ultra: wszystko, w tym pozostałości shaderów, aktualizacji, sterowników, pamięci podręczne aplikacji, Store i ETL", L"Ultra: tudo, incl. restos de shaders, atualizações, drivers, caches de aplicativos, Store e ETL", L"Ultra：シェーダー、更新、ドライバー、アプリのキャッシュ、Store、ETL の残りをすべて含む", L"Ultra：包含著色器、更新、驅動程式、應用程式快取、Store 與 ETL 的所有殘留"}},
    {L"Windows-Komponentenspeicher, Schattenkopien und Ereignisprotokolle – das kann einige Minuten dauern …",
     {L"Windows component store, shadow copies and event logs – this can take a few minutes …", L"Magasin de composants Windows, clichés instantanés et journaux d'événements – cela peut prendre quelques minutes …", L"Almacén de componentes de Windows, instantáneas y registros de eventos – esto puede tardar unos minutos …", L"Magazyn składników systemu Windows, kopie w tle i dzienniki zdarzeń – może to potrwać kilka minut …", L"Armazenamento de componentes do Windows, cópias de sombra e logs de eventos – isso pode levar alguns minutos …", L"Windows のコンポーネントストア、シャドウコピー、イベントログ（数分かかることがあります）…", L"Windows 元件存放區、陰影複製與事件記錄檔（可能需要數分鐘）…"}},
    {L"Windows-Komponentenspeicher (WinSxS) bereinigen?\n\nStartet DISM mit /StartComponentCleanup /ResetBase und entfernt ersetzte Komponentenversionen. Das kann von einigen Minuten bis zu einer halben Stunde dauern; Tidy bitte so lange geöffnet lassen. Danach lassen sich bereits installierte Windows-Updates NICHT mehr deinstallieren. Nicht rückgängig zu machen.\n\nWindows fragt dafür einmal nach Administratorrechten.",
     {L"Clean the Windows component store (WinSxS)?\n\nStarts DISM with /StartComponentCleanup /ResetBase and removes superseded component versions. This can take from a few minutes up to half an hour; please keep Tidy open. Afterwards, already installed Windows updates can NO longer be uninstalled. This cannot be undone.\n\nWindows asks once for administrator rights.",
      L"Nettoyer le magasin de composants Windows (WinSxS) ?\n\nLance DISM avec /StartComponentCleanup /ResetBase et supprime les versions de composants remplacées. Cela peut durer de quelques minutes à une demi-heure ; laissez Tidy ouvert. Ensuite, les mises à jour Windows déjà installées ne pourront PLUS être désinstallées. Irréversible.\n\nWindows demande une fois les droits administrateur.",
      L"¿Limpiar el almacén de componentes de Windows (WinSxS)?\n\nInicia DISM con /StartComponentCleanup /ResetBase y elimina las versiones de componentes reemplazadas. Puede tardar de unos minutos a media hora; deja Tidy abierto. Después ya NO se podrán desinstalar las actualizaciones de Windows instaladas. No se puede deshacer.\n\nWindows pide una vez derechos de administrador.",
      L"Wyczyścić magazyn składników systemu Windows (WinSxS)?\n\nUruchamia DISM z /StartComponentCleanup /ResetBase i usuwa zastąpione wersje składników. Może to potrwać od kilku minut do pół godziny; pozostaw Tidy otwarte. Potem zainstalowanych już aktualizacji systemu Windows NIE będzie można odinstalować. Tego nie można cofnąć.\n\nSystem Windows raz poprosi o uprawnienia administratora.",
      L"Limpar o armazenamento de componentes do Windows (WinSxS)?\n\nInicia o DISM com /StartComponentCleanup /ResetBase e remove versões de componentes substituídas. Pode levar de alguns minutos até meia hora; mantenha o Tidy aberto. Depois, as atualizações do Windows já instaladas NÃO poderão mais ser desinstaladas. Não é possível desfazer.\n\nO Windows pede uma vez direitos de administrador.",
      L"Windows のコンポーネントストア（WinSxS）をクリーンアップしますか？\n\nDISM を /StartComponentCleanup /ResetBase で実行し、置き換えられた古いコンポーネントを削除します。数分から30分ほどかかります。その間は Tidy を開いたままにしてください。実行後は、すでにインストールされた Windows 更新プログラムをアンインストールできなくなります。元に戻せません。\n\n管理者権限の確認が1回表示されます。",
      L"要清理 Windows 元件存放區（WinSxS）嗎？\n\n會以 /StartComponentCleanup /ResetBase 執行 DISM，移除已被取代的元件版本。可能需要數分鐘至半小時，請保持 Tidy 開啟。之後已安裝的 Windows 更新將無法再解除安裝。此操作無法復原。\n\nWindows 會要求一次系統管理員權限。"}},
    {L"Alte Schattenkopien löschen?\n\nLöscht auf jedem Laufwerk alle Schattenkopien (Wiederherstellungspunkte, „Vorgängerversionen“) außer der neuesten. Ältere Wiederherstellungspunkte sind danach weg. Nicht rückgängig zu machen.\n\nBenötigt Administratorrechte.",
     {L"Delete old shadow copies?\n\nDeletes all shadow copies (restore points, “previous versions”) on every drive except the newest one. Older restore points are gone afterwards. This cannot be undone.\n\nRequires administrator rights.",
      L"Supprimer les anciens clichés instantanés ?\n\nSupprime sur chaque lecteur tous les clichés instantanés (points de restauration, « versions précédentes ») sauf le plus récent. Les points de restauration plus anciens disparaissent. Irréversible.\n\nNécessite des droits administrateur.",
      L"¿Eliminar las instantáneas antiguas?\n\nElimina en cada unidad todas las instantáneas (puntos de restauración, «versiones anteriores») excepto la más reciente. Los puntos de restauración más antiguos desaparecen. No se puede deshacer.\n\nRequiere derechos de administrador.",
      L"Usunąć stare kopie w tle?\n\nUsuwa na każdym dysku wszystkie kopie w tle (punkty przywracania, „poprzednie wersje”) oprócz najnowszej. Starsze punkty przywracania znikną. Tego nie można cofnąć.\n\nWymaga uprawnień administratora.",
      L"Excluir cópias de sombra antigas?\n\nExclui em cada unidade todas as cópias de sombra (pontos de restauração, “versões anteriores”), exceto a mais recente. Pontos de restauração mais antigos desaparecem. Não é possível desfazer.\n\nRequer direitos de administrador.",
      L"古いシャドウコピーを削除しますか？\n\n各ドライブで最新のものを除くすべてのシャドウコピー（復元ポイント、「以前のバージョン」）を削除します。それより古い復元ポイントは失われます。元に戻せません。\n\n管理者権限が必要です。",
      L"要刪除舊的陰影複製嗎？\n\n會刪除每個磁碟機上除最新一份以外的所有陰影複製（還原點、「舊版」）。更舊的還原點將消失。此操作無法復原。\n\n需要系統管理員權限。"}},
    {L"Windows-Ereignisprotokolle leeren?\n\nLeert alle Ereignisprotokolle (Anwendung, System, Setup …); das Sicherheitsprotokoll bleibt unberührt. Die Einträge helfen bei der Fehlersuche und sind danach weg. Nicht rückgängig zu machen.\n\nBenötigt Administratorrechte.",
     {L"Clear the Windows event logs?\n\nClears all event logs (Application, System, Setup …); the Security log is left untouched. The entries help with troubleshooting and are gone afterwards. This cannot be undone.\n\nRequires administrator rights.",
      L"Vider les journaux d'événements Windows ?\n\nVide tous les journaux d'événements (Application, Système, Installation …) ; le journal de sécurité n'est pas touché. Les entrées aident au dépannage et disparaissent. Irréversible.\n\nNécessite des droits administrateur.",
      L"¿Vaciar los registros de eventos de Windows?\n\nVacía todos los registros de eventos (Aplicación, Sistema, Instalación …); el registro de seguridad no se toca. Las entradas ayudan a diagnosticar errores y desaparecen. No se puede deshacer.\n\nRequiere derechos de administrador.",
      L"Wyczyścić dzienniki zdarzeń systemu Windows?\n\nCzyści wszystkie dzienniki zdarzeń (Aplikacja, System, Instalacja …); dziennik zabezpieczeń pozostaje nietknięty. Wpisy pomagają w diagnozowaniu błędów i znikną. Tego nie można cofnąć.\n\nWymaga uprawnień administratora.",
      L"Limpar os logs de eventos do Windows?\n\nLimpa todos os logs de eventos (Aplicativo, Sistema, Instalação …); o log de segurança não é alterado. As entradas ajudam na solução de problemas e desaparecem. Não é possível desfazer.\n\nRequer direitos de administrador.",
      L"Windows のイベントログを消去しますか？\n\nすべてのイベントログ（アプリケーション、システム、セットアップなど）を消去します。セキュリティログは変更されません。エラー調査に役立つ記録が失われます。元に戻せません。\n\n管理者権限が必要です。",
      L"要清除 Windows 事件記錄檔嗎？\n\n會清除所有事件記錄檔（應用程式、系統、安裝等），安全性記錄檔不受影響。這些項目有助於疑難排解，清除後將消失。此操作無法復原。\n\n需要系統管理員權限。"}},
    {L"\n\nUltra-Aktionen: Die Administrator-Abfrage wurde abgelehnt oder konnte nicht gestartet werden. Es wurde nichts davon ausgeführt.",
     {L"\n\nUltra actions: the administrator prompt was declined or could not be started. None of them were run.", L"\n\nActions ultra : la demande d'administrateur a été refusée ou n'a pas pu démarrer. Aucune n'a été exécutée.", L"\n\nAcciones ultra: se rechazó la solicitud de administrador o no se pudo iniciar. No se ejecutó ninguna.", L"\n\nAkcje ultra: monit administratora został odrzucony lub nie udało się go uruchomić. Żadna nie została wykonana.", L"\n\nAções ultra: a solicitação de administrador foi recusada ou não pôde ser iniciada. Nenhuma foi executada.", L"\n\nウルトラ操作：管理者権限の確認が拒否されたか、開始できませんでした。どれも実行されていません。", L"\n\n超深度操作：系統管理員提示被拒絕或無法啟動，所有操作均未執行。"}},
    {L"\n\nUltra-Aktionen:",
     {L"\n\nUltra actions:", L"\n\nActions ultra :", L"\n\nAcciones ultra:", L"\n\nAkcje ultra:", L"\n\nAções ultra:", L"\n\nウルトラ操作：", L"\n\n超深度操作："}},
    {L"Komponentenspeicher (DISM)",
     {L"Component store (DISM)", L"Magasin de composants (DISM)", L"Almacén de componentes (DISM)", L"Magazyn składników (DISM)", L"Armazenamento de componentes (DISM)", L"コンポーネントストア（DISM）", L"元件存放區（DISM）"}},
    {L"Alte Schattenkopien",
     {L"Old shadow copies", L"Anciens clichés instantanés", L"Instantáneas antiguas", L"Stare kopie w tle", L"Cópias de sombra antigas", L"古いシャドウコピー", L"舊的陰影複製"}},
    {L"Ereignisprotokolle",
     {L"Event logs", L"Journaux d'événements", L"Registros de eventos", L"Dzienniki zdarzeń", L"Logs de eventos", L"イベントログ", L"事件記錄檔"}},
    {L"fehlgeschlagen",
     {L"failed", L"échec", L"fallido", L"nie powiodło się", L"falhou", L"失敗", L"失敗"}},
    {L"erledigt",
     {L"done", L"terminé", L"hecho", L"gotowe", L"concluído", L"完了", L"完成"}},
    // Ultra-Tiefenreinigung (Deep Clean)
    {L"Ultra-Tiefenreinigung: Vor dem Löschen kommt eine Vorschau je Gruppe. Systemordner, Schutzliste und benutzte Dateien bleiben erhalten.",
     {L"Ultra deep clean: a preview per group comes before deleting. System folders, protection list and files in use are kept.", L"Nettoyage ultra-approfondi : un aperçu par groupe précède la suppression. Les dossiers système, la liste de protection et les fichiers utilisés sont conservés.", L"Limpieza ultra profunda: antes de borrar se muestra una vista previa por grupo. Las carpetas del sistema, la lista de protección y los archivos en uso se conservan.", L"Czyszczenie ultra-głębokie: przed usunięciem pojawia się podgląd dla każdej grupy. Foldery systemowe, lista ochrony i używane pliki zostają zachowane.", L"Limpeza ultraprofunda: antes de excluir há uma prévia por grupo. Pastas do sistema, lista de proteção e arquivos em uso são mantidos.", L"ウルトラ・ディープクリーン：削除の前にグループごとのプレビューを表示します。システムフォルダー、保護リスト、使用中のファイルは保持されます。", L"超深度清理：刪除前會先顯示各群組的預覽。系統資料夾、保護清單與使用中的檔案都會保留。"}},
    {L"Standard",
     {L"Standard", L"Standard", L"Estándar", L"Standardowe", L"Padrão", L"標準", L"標準"}},
    {L"Ultra-Tiefenreinigung",
     {L"Ultra deep clean", L"Nettoyage ultra-approfondi", L"Limpieza ultra profunda", L"Czyszczenie ultra-głębokie", L"Limpeza ultraprofunda", L"ウルトラ・ディープクリーン", L"超深度清理"}},
    {L"Ultra-Tiefenreinigung",
     {L"Ultra deep clean (advanced)", L"Nettoyage ultra-approfondi (avancé)", L"Limpieza ultra profunda (avanzada)", L"Czyszczenie ultra-głębokie (zaawansowane)", L"Limpeza ultraprofunda (avançada)", L"ウルトラ・ディープクリーン（詳細）", L"超深度清理（進階）"}},
    {L"Nimmt bei den Systemdateien und beim Optimieren alles mit auf, was geht: Shader-Caches, Windows-Update- und Treiber-Installer-Reste, Fehlerberichte, Windows-Temp, App-Caches, Store-Reste und ETL-Protokolle. Auf der Seite Systemdateien kommen auf Wunsch DISM, Schattenkopien und Ereignisprotokolle dazu (jeweils mit Rückfrage und Administratorrechten). Vorher zeigt Tidy eine Vorschau. Auto-Clean und Schnell-Reinigung bleiben beim Standard.",
     {L"Also covers in System files and Optimize everything possible: shader caches, Windows Update and driver installer leftovers, error reports, Windows Temp, app caches, Store leftovers and ETL logs. On the System files page, DISM, shadow copies and event logs can be added on request (each with its own confirmation and administrator rights). Tidy shows a preview first. Auto-Clean and quick clean stay on standard.",
      L"Inclut dans Fichiers système et Optimiser tout ce qui est possible : caches de shaders, restes de Windows Update et d'installateurs de pilotes, rapports d'erreurs, Temp de Windows, caches d'applications, restes du Store et journaux ETL. Sur la page Fichiers système, DISM, clichés instantanés et journaux d'événements peuvent s'y ajouter (chacun avec confirmation et droits administrateur). Tidy affiche d'abord un aperçu. Le nettoyage automatique et rapide reste en mode standard.",
      L"Incluye en Archivos del sistema y Optimizar todo lo posible: cachés de shaders, restos de Windows Update y de instaladores de controladores, informes de errores, Temp de Windows, cachés de aplicaciones, restos de Store y registros ETL. En la página Archivos del sistema se pueden añadir DISM, instantáneas y registros de eventos (cada uno con confirmación y derechos de administrador). Tidy muestra antes una vista previa. La limpieza automática y rápida sigue en modo estándar.",
      L"Obejmuje w plikach systemowych i przy optymalizacji wszystko, co możliwe: pamięci podręczne shaderów, pozostałości Windows Update i instalatorów sterowników, raporty błędów, Temp systemu Windows, pamięci podręczne aplikacji, pozostałości Store i dzienniki ETL. Na stronie Pliki systemowe można dodać DISM, kopie w tle i dzienniki zdarzeń (każde z osobnym potwierdzeniem i uprawnieniami administratora). Tidy najpierw pokazuje podgląd. Automatyczne i szybkie czyszczenie pozostają w trybie standardowym.",
      L"Inclui em Arquivos do sistema e Otimizar tudo o que é possível: caches de shaders, restos do Windows Update e de instaladores de drivers, relatórios de erros, Temp do Windows, caches de aplicativos, restos da Store e logs ETL. Na página Arquivos do sistema podem ser adicionados DISM, cópias de sombra e logs de eventos (cada um com confirmação e direitos de administrador). O Tidy mostra antes uma prévia. A limpeza automática e rápida continuam no modo padrão.",
      L"システムファイルと最適化に、可能なものをすべて含めます：シェーダーキャッシュ、Windows Update とドライバーインストーラーの残り、エラーレポート、Windows の Temp、アプリのキャッシュ、Store の残り、ETL ログ。システムファイルのページでは、DISM、シャドウコピー、イベントログも追加できます（それぞれ個別の確認と管理者権限が必要）。事前にプレビューを表示します。自動クリーンとクイッククリーンは標準のままです。",
      L"在系統檔案與最佳化中包含所有可行項目：著色器快取、Windows Update 與驅動程式安裝程式殘留、錯誤報告、Windows Temp、應用程式快取、Store 殘留與 ETL 記錄。在「系統檔案」頁面可另外加入 DISM、陰影複製與事件記錄檔（每項都有各自的確認並需要系統管理員權限）。清理前會先顯示預覽。自動清理與快速清理仍維持標準模式。"}},
    {L"Shader-Caches (DirectX, NVIDIA, AMD)",
     {L"Shader caches (DirectX, NVIDIA, AMD)", L"Caches de shaders (DirectX, NVIDIA, AMD)", L"Cachés de shaders (DirectX, NVIDIA, AMD)", L"Pamięci podręczne shaderów (DirectX, NVIDIA, AMD)", L"Caches de shaders (DirectX, NVIDIA, AMD)", L"シェーダーキャッシュ（DirectX、NVIDIA、AMD）", L"著色器快取（DirectX、NVIDIA、AMD）"}},
    {L"Windows-Update-Reste",
     {L"Windows Update leftovers", L"Restes de Windows Update", L"Restos de Windows Update", L"Pozostałości Windows Update", L"Restos do Windows Update", L"Windows Update の残り", L"Windows Update 殘留檔案"}},
    {L"Treiber-Installer-Reste",
     {L"Driver installer leftovers", L"Restes d'installateurs de pilotes", L"Restos de instaladores de controladores", L"Pozostałości instalatorów sterowników", L"Restos de instaladores de drivers", L"ドライバーインストーラーの残り", L"驅動程式安裝程式殘留"}},
    {L"Setup-Reste (Windows-Temp)",
     {L"Setup leftovers (Windows Temp)", L"Restes d'installation (Temp de Windows)", L"Restos de instalación (Temp de Windows)", L"Pozostałości instalacji (Temp systemu Windows)", L"Restos de instalação (Temp do Windows)", L"セットアップの残り（Windows Temp）", L"安裝殘留（Windows Temp）"}},
    {L"Von Windows gesammelte Fehlerberichte (Benutzer und System)",
     {L"Error reports collected by Windows (user and system)", L"Rapports d'erreurs collectés par Windows (utilisateur et système)", L"Informes de errores recopilados por Windows (usuario y sistema)", L"Raporty błędów zebrane przez system Windows (użytkownik i system)", L"Relatórios de erros coletados pelo Windows (usuário e sistema)", L"Windows が収集したエラーレポート（ユーザーとシステム）", L"Windows 收集的錯誤報告（使用者與系統）"}},
    {L"Zwischengespeicherte Shader der Grafiktreiber, werden beim nächsten Programm- oder Spielstart neu aufgebaut",
     {L"Cached shaders of the graphics drivers, rebuilt at the next program or game start", L"Shaders mis en cache par les pilotes graphiques, reconstruits au prochain lancement d'un programme ou d'un jeu", L"Shaders en caché de los controladores gráficos; se reconstruyen en el próximo inicio de un programa o juego", L"Zapisane w pamięci podręcznej shadery sterowników graficznych, odbudowywane przy następnym uruchomieniu programu lub gry", L"Shaders em cache dos drivers gráficos, recriados na próxima abertura de um programa ou jogo", L"グラフィックドライバーのシェーダーキャッシュ。次回のアプリやゲーム起動時に再作成されます", L"顯示驅動程式的著色器快取，下次啟動程式或遊戲時會重新建立"}},
    {L"Heruntergeladene Update-Pakete, Übermittlungsoptimierung und Update-Protokolle. Gesperrtes bleibt (ohne Administratorrechte)",
     {L"Downloaded update packages, delivery optimization and update logs. Locked items stay (without administrator rights)", L"Paquets de mise à jour téléchargés, optimisation de la distribution et journaux de mise à jour. Les éléments verrouillés restent (sans droits administrateur)", L"Paquetes de actualización descargados, optimización de distribución y registros de actualización. Lo bloqueado se conserva (sin derechos de administrador)", L"Pobrane pakiety aktualizacji, optymalizacja dostarczania i dzienniki aktualizacji. Zablokowane elementy pozostają (bez uprawnień administratora)", L"Pacotes de atualização baixados, otimização de entrega e logs de atualização. Itens bloqueados permanecem (sem direitos de administrador)", L"ダウンロード済みの更新パッケージ、配信の最適化、更新ログ。使用中のものは残ります（管理者権限がない場合）", L"已下載的更新套件、傳遞最佳化與更新記錄。被鎖定的項目會保留（無系統管理員權限時）"}},
    {L"Entpackte Grafiktreiber-Installer in C:\\NVIDIA und C:\\AMD – der installierte Treiber bleibt unberührt",
     {L"Extracted graphics driver installers in C:\\NVIDIA and C:\\AMD – the installed driver is not touched", L"Installateurs de pilotes graphiques extraits dans C:\\NVIDIA et C:\\AMD – le pilote installé n'est pas touché", L"Instaladores de controladores gráficos extraídos en C:\\NVIDIA y C:\\AMD – el controlador instalado no se toca", L"Rozpakowane instalatory sterowników graficznych w C:\\NVIDIA i C:\\AMD – zainstalowany sterownik pozostaje nietknięty", L"Instaladores de drivers gráficos extraídos em C:\\NVIDIA e C:\\AMD – o driver instalado não é alterado", L"C:\\NVIDIA と C:\\AMD に展開されたグラフィックドライバーのインストーラー。インストール済みのドライバーには影響しません", L"解壓縮在 C:\\NVIDIA 與 C:\\AMD 的顯示驅動程式安裝檔，已安裝的驅動程式不受影響"}},
    {L"Temporäre Setup-Dateien des Systems. Gesperrtes bleibt (ohne Administratorrechte)",
     {L"Temporary setup files of the system. Locked items stay (without administrator rights)", L"Fichiers d'installation temporaires du système. Les éléments verrouillés restent (sans droits administrateur)", L"Archivos temporales de instalación del sistema. Lo bloqueado se conserva (sin derechos de administrador)", L"Tymczasowe pliki instalacyjne systemu. Zablokowane elementy pozostają (bez uprawnień administratora)", L"Arquivos temporários de instalação do sistema. Itens bloqueados permanecem (sem direitos de administrador)", L"システムの一時的なセットアップファイル。使用中のものは残ります（管理者権限がない場合）", L"系統的暫存安裝檔案。被鎖定的項目會保留（無系統管理員權限時）"}},
    {L"Ultra-Tiefenreinigung: auch Shader-, Update- und Treiber-Reste",
     {L"Ultra deep clean: also shader, update and driver leftovers", L"Nettoyage ultra-approfondi : aussi les restes de shaders, mises à jour et pilotes", L"Limpieza ultra profunda: también restos de shaders, actualizaciones y controladores", L"Czyszczenie ultra-głębokie: także pozostałości shaderów, aktualizacji i sterowników", L"Limpeza ultraprofunda: também restos de shaders, atualizações e drivers", L"ウルトラ・ディープクリーン：シェーダー・更新・ドライバーの残りも対象", L"超深度清理：包含著色器、更新與驅動程式殘留"}},
    {L"Ultra-Tiefenreinigung aktiv",
     {L"Ultra deep clean active", L"Nettoyage ultra-approfondi actif", L"Limpieza ultra profunda activa", L"Czyszczenie ultra-głębokie włączone", L"Limpeza ultraprofunda ativa", L"ウルトラ・ディープクリーン有効", L"超深度清理已啟用"}},
    {L"Ultra-Tiefenreinigung – diese Systemdateien werden ENDGÜLTIG gelöscht:",
     {L"Ultra deep clean – these system files will be deleted PERMANENTLY:", L"Nettoyage ultra-approfondi – ces fichiers système seront supprimés DÉFINITIVEMENT :", L"Limpieza ultra profunda: estos archivos del sistema se borrarán de forma DEFINITIVA:", L"Czyszczenie ultra-głębokie – te pliki systemowe zostaną usunięte NA STAŁE:", L"Limpeza ultraprofunda – estes arquivos do sistema serão excluídos DEFINITIVAMENTE:", L"ウルトラ・ディープクリーン：次のシステムファイルを完全に削除します：", L"超深度清理：下列系統檔案將被永久刪除："}},
    {L"\n\nGeschützte Dateien (Schutzliste) und Systemordner bleiben erhalten; benutzte Dateien werden übersprungen. Nicht wiederherstellbar.",
     {L"\n\nProtected files (protection list) and system folders are kept; files in use are skipped. Cannot be restored.", L"\n\nLes fichiers protégés (liste de protection) et les dossiers système sont conservés ; les fichiers utilisés sont ignorés. Non récupérable.", L"\n\nLos archivos protegidos (lista de protección) y las carpetas del sistema se conservan; los archivos en uso se omiten. No se puede recuperar.", L"\n\nPliki chronione (lista ochrony) i foldery systemowe zostają zachowane; używane pliki są pomijane. Nie można ich przywrócić.", L"\n\nArquivos protegidos (lista de proteção) e pastas do sistema são mantidos; arquivos em uso são ignorados. Não é possível restaurar.", L"\n\n保護されたファイル（保護リスト）とシステムフォルダーは保持され、使用中のファイルはスキップされます。元に戻せません。", L"\n\n受保護的檔案（保護清單）與系統資料夾會保留，使用中的檔案會略過。無法復原。"}},
    {L"\n\nUltra-Tiefenreinigung – Systemdateien im Detail:",
     {L"\n\nDeep clean – system files in detail:", L"\n\nNettoyage ultra-approfondi – fichiers système en détail :", L"\n\nLimpieza ultra profunda: archivos del sistema en detalle:", L"\n\nCzyszczenie ultra-głębokie – pliki systemowe w szczegółach:", L"\n\nLimpeza ultraprofunda – arquivos do sistema em detalhe:", L"\n\nウルトラ・ディープクリーン：システムファイルの内訳：", L"\n\n超深度清理：系統檔案明細："}},
    {L"Symbol- und Vorschau-Cache des Explorers lassen sich nur löschen, wenn der Windows-Explorer dabei kurz beendet wird (Ordnerfenster und Taskleiste verschwinden für einen Moment und kommen von selbst zurück).\n\nExplorer dafür kurz neu starten?\n\nNein = diese Caches diesmal auslassen.",
     {L"The Explorer's icon and thumbnail caches can only be deleted while Windows Explorer is briefly closed (folder windows and taskbar disappear for a moment and come back by themselves).\n\nRestart Explorer briefly for this?\n\nNo = skip these caches this time.",
      L"Le cache des icônes et des miniatures de l'Explorateur ne peut être supprimé que si l'Explorateur Windows est brièvement fermé (les fenêtres de dossiers et la barre des tâches disparaissent un instant et reviennent seules).\n\nRedémarrer brièvement l'Explorateur ?\n\nNon = ignorer ces caches cette fois.",
      L"La caché de iconos y miniaturas del Explorador solo se puede borrar si el Explorador de Windows se cierra brevemente (las ventanas de carpetas y la barra de tareas desaparecen un momento y vuelven solas).\n\n¿Reiniciar brevemente el Explorador?\n\nNo = omitir estas cachés esta vez.",
      L"Pamięć podręczną ikon i miniatur Eksploratora można usunąć tylko po krótkim zamknięciu Eksploratora Windows (okna folderów i pasek zadań znikają na chwilę i wracają same).\n\nUruchomić Eksploratora krótko ponownie?\n\nNie = tym razem pomiń te pamięci podręczne.",
      L"O cache de ícones e miniaturas do Explorador só pode ser excluído se o Explorador do Windows for fechado brevemente (as janelas de pastas e a barra de tarefas somem por um momento e voltam sozinhas).\n\nReiniciar o Explorador rapidamente?\n\nNão = ignorar esses caches desta vez.",
      L"エクスプローラーのアイコンとサムネイルのキャッシュは、Windows エクスプローラーを一時的に終了している間だけ削除できます（フォルダーウィンドウとタスクバーが一瞬消え、自動的に戻ります）。\n\nエクスプローラーを一時的に再起動しますか？\n\nいいえ＝今回はこれらのキャッシュを除外します。",
      L"檔案總管的圖示與縮圖快取，只有在短暫結束 Windows 檔案總管時才能刪除（資料夾視窗與工作列會暫時消失，之後自動回來）。\n\n要短暫重新啟動檔案總管嗎？\n\n否＝這次略過這些快取。"}},
    {L"Dieser Ordner wurde nicht gefunden oder ist nicht lesbar.",
     {L"This folder was not found or cannot be read.", L"Ce dossier est introuvable ou illisible.", L"No se encontró la carpeta o no se puede leer.", L"Nie znaleziono folderu lub nie można go odczytać.", L"A pasta não foi encontrada ou não pode ser lida.", L"このフォルダーが見つからないか、読み取れません。", L"找不到此資料夾，或無法讀取。"}},
    {L"Mehr",
     {L"More", L"Plus", L"Más", L"Więcej", L"Mais", L"その他", L"更多"}},
    {L"Weniger",
     {L"Less", L"Moins", L"Menos", L"Mniej", L"Menos", L"閉じる", L"收起"}},
    {L"Arbeitsspeicher",
     {L"Memory", L"Mémoire", L"Memoria", L"Pamięć", L"Memória", L"メモリ", L"記憶體"}},
    {L"Belegung und Optimierung",
     {L"Usage and optimization", L"Utilisation et optimisation", L"Uso y optimización", L"Użycie i optymalizacja", L"Uso e otimização", L"使用状況と最適化", L"使用量與最佳化"}},
    {L"  ·  nur Anzeige, es wird nichts gelöscht",
     {L"  ·  display only, nothing is deleted", L"  ·  affichage seul, rien n'est supprimé", L"  ·  solo vista, no se borra nada", L"  ·  tylko podgląd, nic nie jest usuwane", L"  ·  apenas exibição, nada é excluído", L"  ·  表示のみ、何も削除されません", L"  ·  僅供顯示，不會刪除任何東西"}},
    {L"%d %% belegt  ·  %s in Benutzung",
     {L"%d %% used  ·  %s in use", L"%d %% utilisé  ·  %s en cours d'utilisation", L"%d %% usado  ·  %s en uso", L"%d %% zajęte  ·  %s w użyciu", L"%d %% usado  ·  %s em uso", L"%d %% 使用中  ·  %s を使用", L"已使用 %d %%  ·  使用中 %s"}},
    {L"„RAM optimieren“ verkleinert den Arbeitsspeicher laufender Programme. Windows holt sich benötigte Daten bei Bedarf zurück; mit Administrator-Rechten wird zusätzlich die Standby-Liste geleert.",
     {L"“Optimize RAM” trims the memory of running programs. Windows takes back the data it needs on demand; with administrator rights the standby list is cleared as well.",
      L"« Optimiser la RAM » réduit la mémoire des programmes en cours. Windows récupère les données nécessaires à la demande ; avec les droits d'administrateur, la liste de secours est aussi vidée.",
      L"«Optimizar la RAM» reduce la memoria de los programas en ejecución. Windows recupera los datos que necesita bajo demanda; con derechos de administrador también se vacía la lista de espera.",
      L"„Optymalizuj RAM” zmniejsza pamięć uruchomionych programów. Windows odzyskuje potrzebne dane w razie potrzeby; z uprawnieniami administratora czyszczona jest też lista gotowości.",
      L"“Otimizar a RAM” reduz a memória dos programas em execução. O Windows recupera os dados necessários sob demanda; com direitos de administrador, a lista de espera também é limpa.",
      L"「RAM を最適化」は実行中のプログラムのメモリを縮小します。必要なデータは Windows が必要に応じて戻します。管理者権限がある場合はスタンバイ リストもクリアされます。",
      L"「最佳化記憶體」會縮減執行中程式的記憶體。Windows 會在需要時取回所需資料；具有系統管理員權限時，也會清空待命清單。"}},
    {L"RAM optimieren",
     {L"Optimize RAM", L"Optimiser la RAM", L"Optimizar la RAM", L"Optymalizuj RAM", L"Otimizar a RAM", L"RAM を最適化", L"最佳化記憶體"}},
    {L"Arbeitsspeicher optimiert.\n\nFreigegeben: %s\nVerfügbar jetzt: %s von %s\nVerkleinerte Prozesse: %d",
     {L"Memory optimized.\n\nFreed: %s\nNow available: %s of %s\nProcesses trimmed: %d",
      L"Mémoire optimisée.\n\nLibéré : %s\nDisponible maintenant : %s sur %s\nProcessus réduits : %d",
      L"Memoria optimizada.\n\nLiberado: %s\nDisponible ahora: %s de %s\nProcesos reducidos: %d",
      L"Pamięć zoptymalizowana.\n\nZwolniono: %s\nDostępne teraz: %s z %s\nZmniejszone procesy: %d",
      L"Memória otimizada.\n\nLiberado: %s\nDisponível agora: %s de %s\nProcessos reduzidos: %d",
      L"メモリを最適化しました。\n\n解放: %s\n現在の空き: %s / %s\n縮小したプロセス: %d",
      L"記憶體已最佳化。\n\n已釋放：%s\n目前可用：%s / %s\n已縮減的處理程序：%d"}},
    {L"\n\nDie Standby-Liste wurde geleert.",
     {L"\n\nThe standby list was cleared.", L"\n\nLa liste de secours a été vidée.", L"\n\nSe vació la lista de espera.", L"\n\nLista gotowości została wyczyszczona.", L"\n\nA lista de espera foi limpa.", L"\n\nスタンバイ リストをクリアしました。", L"\n\n已清空待命清單。"}},
    {L"\n\nHinweis: Ohne Administrator-Rechte wird nur der Arbeitsspeicher der Programme verkleinert, nicht die Standby-Liste.",
     {L"\n\nNote: Without administrator rights only the programs' memory is trimmed, not the standby list.",
      L"\n\nRemarque : sans droits d'administrateur, seule la mémoire des programmes est réduite, pas la liste de secours.",
      L"\n\nNota: sin derechos de administrador solo se reduce la memoria de los programas, no la lista de espera.",
      L"\n\nUwaga: bez uprawnień administratora zmniejszana jest tylko pamięć programów, a nie lista gotowości.",
      L"\n\nObservação: sem direitos de administrador, apenas a memória dos programas é reduzida, não a lista de espera.",
      L"\n\n注意: 管理者権限がない場合、プログラムのメモリのみが縮小され、スタンバイ リストはクリアされません。",
      L"\n\n注意：沒有系統管理員權限時，只會縮減程式的記憶體，不會清空待命清單。"}},
    {L"Auto-Clean ist aus",
     {L"Auto-Clean is off", L"Auto-Clean est désactivé", L"Auto-Clean está desactivado", L"Auto-Clean jest wyłączony", L"O Auto-Clean está desativado", L"Auto-Clean はオフです", L"Auto-Clean 已關閉"}},
    {L"Auto-Clean startet beim ersten Lauf",
     {L"Auto-Clean starts on the first run", L"Auto-Clean démarre au premier lancement", L"Auto-Clean empieza en la primera ejecución", L"Auto-Clean rusza przy pierwszym uruchomieniu", L"O Auto-Clean começa na primeira execução", L"Auto-Clean は初回の起動時に始まります", L"Auto-Clean 會在第一次執行時開始"}},
    {L"Auto-Clean in ",
     {L"Auto-Clean in ", L"Auto-Clean dans ", L"Auto-Clean en ", L"Auto-Clean za ", L"Auto-Clean em ", L"Auto-Clean まであと ", L"Auto-Clean 還有 "}},
    {L" Tagen",
     {L" days", L" jours", L" días", L" dni", L" dias", L" 日", L" 天"}},
    {L"Auto-Clean fällig beim nächsten Login",
     {L"Auto-Clean is due at the next login", L"Auto-Clean prévu à la prochaine ouverture de session", L"Auto-Clean pendiente en el próximo inicio de sesión", L"Auto-Clean zaplanowany przy następnym logowaniu", L"Auto-Clean pendente no próximo login", L"Auto-Clean は次回のサインイン時に実行されます", L"Auto-Clean 將在下次登入時執行"}},
    {L"Soll der Papierkorb jetzt endgültig geleert werden?\n",
     {L"Do you want to permanently empty the Recycle Bin now?\n", L"Voulez-vous vider définitivement la Corbeille maintenant ?\n", L"¿Vaciar definitivamente la Papelera de reciclaje ahora?\n", L"Czy opróżnić teraz Kosz na stałe?\n", L"Deseja esvaziar definitivamente a Lixeira agora?\n", L"ごみ箱を今すぐ完全に空にしますか？\n", L"要現在永久清空資源回收筒嗎？\n"}},
    {L"(Das betrifft den ganzen Papierkorb und kann nicht rückgängig gemacht werden.)",
     {L"(This affects the entire Recycle Bin and cannot be undone.)", L"(Cela concerne toute la Corbeille et est irréversible.)", L"(Esto afecta a toda la Papelera y no se puede deshacer.)", L"(Dotyczy całego Kosza i nie można tego cofnąć.)", L"(Isso afeta toda a Lixeira e não pode ser desfeito.)", L"(ごみ箱全体が対象で、元に戻せません。)", L"(這會影響整個資源回收筒，且無法復原。)"}},
    {L"Papierkorb geleert.",
     {L"Recycle Bin emptied.", L"Corbeille vidée.", L"Papelera vaciada.", L"Kosz opróżniony.", L"Lixeira esvaziada.", L"ごみ箱を空にしました。", L"資源回收筒已清空。"}},
    {L"Papierkorb konnte nicht geleert werden.",
     {L"The Recycle Bin could not be emptied.", L"Impossible de vider la Corbeille.", L"No se pudo vaciar la Papelera.", L"Nie udało się opróżnić Kosza.", L"Não foi possível esvaziar a Lixeira.", L"ごみ箱を空にできませんでした。", L"無法清空資源回收筒。"}},
    {L"Download fehlgeschlagen. Bitte später erneut versuchen.",
     {L"Download failed. Please try again later.", L"Échec du téléchargement. Veuillez réessayer plus tard.", L"Error en la descarga. Inténtalo de nuevo más tarde.", L"Pobieranie nie powiodło się. Spróbuj ponownie później.", L"Falha no download. Tente novamente mais tarde.", L"ダウンロードに失敗しました。後でもう一度お試しください。", L"下載失敗。請稍後再試。"}},
    {L"Die heruntergeladene Datei ist ungültig (Prüfsumme stimmt nicht). Update abgebrochen.",
     {L"The downloaded file is invalid (checksum mismatch). Update cancelled.", L"Le fichier téléchargé est invalide (somme de contrôle incorrecte). Mise à jour annulée.", L"El archivo descargado no es válido (la suma de comprobación no coincide). Actualización cancelada.", L"Pobrany plik jest nieprawidłowy (suma kontrolna się nie zgadza). Aktualizacja anulowana.", L"O arquivo baixado é inválido (a soma de verificação não confere). Atualização cancelada.", L"ダウンロードしたファイルが無効です (チェックサムが一致しません)。更新を中止しました。", L"下載的檔案無效 (總和檢查碼不符)。更新已取消。"}},
    {L"Die Signatur des Updates ist ungültig oder fehlt. Aus Sicherheitsgründen wird es NICHT installiert.",
     {L"The update's signature is invalid or missing. For security reasons it will NOT be installed.", L"La signature de la mise à jour est invalide ou absente. Par sécurité, elle ne sera PAS installée.", L"La firma de la actualización no es válida o falta. Por seguridad, NO se instalará.", L"Podpis aktualizacji jest nieprawidłowy lub go brakuje. Ze względów bezpieczeństwa NIE zostanie zainstalowana.", L"A assinatura da atualização é inválida ou está ausente. Por segurança, ela NÃO será instalada.", L"更新の署名が無効か、見つかりません。安全のため、インストールしません。", L"更新的簽章無效或遺失。基於安全考量，將「不會」安裝。"}},
    {L"Konnte die neue Datei nicht speichern.",
     {L"Could not save the new file.", L"Impossible d'enregistrer le nouveau fichier.", L"No se pudo guardar el archivo nuevo.", L"Nie udało się zapisać nowego pliku.", L"Não foi possível salvar o novo arquivo.", L"新しいファイルを保存できませんでした。", L"無法儲存新檔案。"}},
    {L"Das Update ist nicht neuer als die installierte Version und wird nicht installiert.",
     {L"The update is not newer than the installed version and will not be installed.", L"La mise à jour n'est pas plus récente que la version installée et ne sera pas installée.", L"La actualización no es más reciente que la versión instalada y no se instalará.", L"Aktualizacja nie jest nowsza niż zainstalowana wersja i nie zostanie zainstalowana.", L"A atualização não é mais recente que a versão instalada e não será instalada.", L"この更新はインストール済みのバージョンより新しくないため、インストールしません。", L"此更新不比已安裝的版本新，因此不會安裝。"}},
    {L"Update konnte nicht installiert werden.",
     {L"The update could not be installed.", L"Impossible d'installer la mise à jour.", L"No se pudo instalar la actualización.", L"Nie udało się zainstalować aktualizacji.", L"Não foi possível instalar a atualização.", L"更新をインストールできませんでした。", L"無法安裝更新。"}},
    {L"Wechseldatenträger",
     {L"Removable drive", L"Disque amovible", L"Unidad extraíble", L"Dysk wymienny", L"Unidade removível", L"リムーバブルドライブ", L"卸除式磁碟機"}},
    {L"Lokaler Datenträger",
     {L"Local disk", L"Disque local", L"Disco local", L"Dysk lokalny", L"Disco local", L"ローカルディスク", L"本機磁碟"}},
    {L"Gesundheit nicht verfügbar (z. B. bei USB-Laufwerken)",
     {L"Health not available (e.g. for USB drives)", L"État de santé indisponible (p. ex. pour les clés USB)", L"Estado de salud no disponible (p. ej. en unidades USB)", L"Stan dysku niedostępny (np. dla pamięci USB)", L"Integridade indisponível (p. ex. em unidades USB)", L"正常性は利用できません (USB ドライブなど)", L"無法取得健康狀態 (例如 USB 磁碟機)"}},
    {L"\nWindows meldet für dieses Laufwerk keine Gesundheitswerte.",
     {L"\nWindows does not report any health values for this drive.", L"\nWindows ne fournit aucune valeur de santé pour ce lecteur.", L"\nWindows no informa de valores de salud para esta unidad.", L"\nWindows nie podaje wartości stanu dla tego dysku.", L"\nO Windows não informa valores de integridade para esta unidade.", L"\nWindows はこのドライブの正常性の値を報告していません。", L"\nWindows 未回報此磁碟機的健康狀態值。"}},
    {L"Gesund",
     {L"Healthy", L"Bon", L"Correcto", L"Dobry", L"Íntegro", L"正常", L"良好"}},
    {L"Warnung",
     {L"Warning", L"Avertissement", L"Advertencia", L"Ostrzeżenie", L"Aviso", L"警告", L"警告"}},
    {L"Fehlerhaft",
     {L"Unhealthy", L"Défectueux", L"Defectuoso", L"Uszkodzony", L"Com defeito", L"異常", L"異常"}},
    {L"Gesundheit nicht verfügbar",
     {L"Health not available", L"État de santé indisponible", L"Estado de salud no disponible", L"Stan dysku niedostępny", L"Integridade indisponível", L"正常性は利用できません", L"無法取得健康狀態"}},
    {L"Abnutzung %.0f %%",
     {L"Wear %.0f %%", L"Usure %.0f %%", L"Desgaste %.0f %%", L"Zużycie %.0f %%", L"Desgaste %.0f %%", L"消耗 %.0f %%", L"損耗 %.0f %%"}},
    {L"%.0f Std. in Betrieb",
     {L"%.0f h powered on", L"%.0f h de fonctionnement", L"%.0f h en funcionamiento", L"%.0f godz. pracy", L"%.0f h em funcionamento", L"稼働 %.0f 時間", L"已運作 %.0f 小時"}},
    {L"%.0f Lesefehler",
     {L"%.0f read errors", L"%.0f erreurs de lecture", L"%.0f errores de lectura", L"%.0f błędów odczytu", L"%.0f erros de leitura", L"読み取りエラー %.0f 件", L"%.0f 次讀取錯誤"}},
    {L"%.0f Schreibfehler",
     {L"%.0f write errors", L"%.0f erreurs d'écriture", L"%.0f errores de escritura", L"%.0f błędów zapisu", L"%.0f erros de gravação", L"書き込みエラー %.0f 件", L"%.0f 次寫入錯誤"}},
    {L"Betriebsstatus: ",
     {L"Operational status: ", L"État de fonctionnement : ", L"Estado operativo: ", L"Stan operacyjny: ", L"Status operacional: ", L"動作状態： ", L"運作狀態： "}},
    {L"Gesund, aber auffällig",
     {L"Healthy, but noteworthy", L"Bon, mais à surveiller", L"Correcto, pero llamativo", L"Dobry, ale zwraca uwagę", L"Íntegro, mas chama atenção", L"正常ですが注意が必要です", L"良好，但需留意"}},
    {L"nicht verfügbar",
     {L"not available", L"indisponible", L"no disponible", L"niedostępne", L"indisponível", L"利用できません", L"無法取得"}},
    {L"\nDatenträger ",
     {L"\nDisk ", L"\nDisque ", L"\nDisco ", L"\nDysk ", L"\nDisco ", L"\nディスク ", L"\n磁碟 "}},
    {L"\nTyp: ",
     {L"\nType: ", L"\nType : ", L"\nTipo: ", L"\nTyp: ", L"\nTipo: ", L"\n種類： ", L"\n類型： "}},
    {L"unbekannt",
     {L"unknown", L"inconnu", L"desconocido", L"nieznany", L"desconhecido", L"不明", L"未知"}},
    {L"\nDateisystem: ",
     {L"\nFile system: ", L"\nSystème de fichiers : ", L"\nSistema de archivos: ", L"\nSystem plików: ", L"\nSistema de arquivos: ", L"\nファイルシステム： ", L"\n檔案系統： "}},
    {L"\nStatus: ",
     {L"\nStatus: ", L"\nÉtat : ", L"\nEstado: ", L"\nStan: ", L"\nStatus: ", L"\n状態： ", L"\n狀態： "}},
    {L"\nTemperatur: ",
     {L"\nTemperature: ", L"\nTempérature : ", L"\nTemperatura: ", L"\nTemperatura: ", L"\nTemperatura: ", L"\n温度： ", L"\n溫度： "}},
    {L"\nAbnutzung: ",
     {L"\nWear: ", L"\nUsure : ", L"\nDesgaste: ", L"\nZużycie: ", L"\nDesgaste: ", L"\n消耗： ", L"\n損耗： "}},
    {L"\nBetriebsstunden: ",
     {L"\nPower-on hours: ", L"\nHeures de fonctionnement : ", L"\nHoras de funcionamiento: ", L"\nGodziny pracy: ", L"\nHoras de funcionamento: ", L"\n稼働時間： ", L"\n運作時數： "}},
    {L"\nLesefehler: ",
     {L"\nRead errors: ", L"\nErreurs de lecture : ", L"\nErrores de lectura: ", L"\nBłędy odczytu: ", L"\nErros de leitura: ", L"\n読み取りエラー： ", L"\n讀取錯誤： "}},
    {L"\nSchreibfehler: ",
     {L"\nWrite errors: ", L"\nErreurs d'écriture : ", L"\nErrores de escritura: ", L"\nBłędy zapisu: ", L"\nErros de gravação: ", L"\n書き込みエラー： ", L"\n寫入錯誤： "}},
    {L"Laufwerk",
     {L"Drive", L"Lecteur", L"Unidad", L"Dysk", L"Unidade", L"ドライブ", L"磁碟機"}},
    {L"Nicht installiert oder noch nie benutzt",
     {L"Not installed or never used", L"Non installé ou jamais utilisé", L"No instalado o nunca usado", L"Niezainstalowana lub nigdy nieużywana", L"Não instalado ou nunca usado", L"未インストール、または一度も使用されていません", L"未安裝或從未使用"}},
    {L" Cache  ·  ",
     {L" cache  ·  ", L" de cache  ·  ", L" de caché  ·  ", L" pamięci podręcznej  ·  ", L" de cache  ·  ", L" のキャッシュ  ·  ", L" 快取  ·  "}},
    {L" Ordner",
     {L" folders", L" dossiers", L" carpetas", L" folderów", L" pastas", L" フォルダー", L" 個資料夾"}},
    {L"Cache ist leer",
     {L"Cache is empty", L"Le cache est vide", L"La caché está vacía", L"Pamięć podręczna jest pusta", L"O cache está vazio", L"キャッシュは空です", L"快取是空的"}},
    {L"  ·  läuft gerade, bitte schließen",
     {L"  ·  currently running, please close it", L"  ·  en cours d'exécution, veuillez le fermer", L"  ·  en ejecución, ciérralo por favor", L"  ·  jest uruchomiona, zamknij ją", L"  ·  em execução, feche-o por favor", L"  ·  実行中です。閉じてください", L"  ·  正在執行，請先關閉"}},
    {L"\n… und ",
     {L"\n… and ", L"\n… et ", L"\n… y ", L"\n… i ", L"\n… e ", L"\n… ほか ", L"\n… 還有 "}},
    {L" weitere",
     {L" more", L" autres", L" más", L" więcej", L" outros", L" 件", L" 項"}},
    {L"Analysiere Laufwerke …",
     {L"Analyzing drives …", L"Analyse des lecteurs …", L"Analizando unidades …", L"Analizowanie dysków …", L"Analisando unidades …", L"ドライブを分析中 …", L"正在分析磁碟機 …"}},
    {L"Keine Laufwerke gefunden",
     {L"No drives found", L"Aucun lecteur trouvé", L"No se encontraron unidades", L"Nie znaleziono dysków", L"Nenhuma unidade encontrada", L"ドライブが見つかりません", L"找不到磁碟機"}},
    {L"Analysiere Browser …",
     {L"Analyzing browsers …", L"Analyse des navigateurs …", L"Analizando navegadores …", L"Analizowanie przeglądarek …", L"Analisando navegadores …", L"ブラウザーを分析中 …", L"正在分析瀏覽器 …"}},
    {L"Keine Autostart-Programme gefunden",
     {L"No autostart programs found", L"Aucun programme de démarrage trouvé", L"No se encontraron programas de inicio", L"Nie znaleziono programów autostartu", L"Nenhum programa de inicialização encontrado", L"スタートアッププログラムが見つかりません", L"找不到啟動程式"}},
    {L"Keine Treffer",
     {L"No matches", L"Aucun résultat", L"Sin resultados", L"Brak wyników", L"Nenhum resultado", L"一致なし", L"沒有符合的項目"}},
    {L"%d %% belegt",
     {L"%d %% used", L"%d %% utilisé", L"%d %% ocupado", L"%d %% zajęte", L"%d %% ocupado", L"%d %% 使用中", L"已使用 %d %%"}},
    {L" frei von ",
     {L" free of ", L" libres sur ", L" libres de ", L" wolne z ", L" livres de ", L" 空き / ", L" 可用 / 共 "}},
    {L"An",
     {L"On", L"Actif", L"Activo", L"Wł.", L"Ativo", L"オン", L"開"}},
    {L"Leeren",
     {L"Clear", L"Vider", L"Vaciar", L"Wyczyść", L"Limpar", L"削除", L"清除"}},
    {L"Aus",
     {L"Off", L"Inactif", L"Inactivo", L"Wył.", L"Inativo", L"オフ", L"關"}},
    {L"Autostart-Ordner",
     {L"Startup folder", L"Dossier Démarrage", L"Carpeta de inicio", L"Folder Autostart", L"Pasta de inicialização", L"スタートアップフォルダー", L"啟動資料夾"}},
    {L"Für alle Benutzer",
     {L"For all users", L"Pour tous les utilisateurs", L"Para todos los usuarios", L"Dla wszystkich użytkowników", L"Para todos os usuários", L"すべてのユーザー", L"所有使用者"}},
    {L"Nur für dich",
     {L"Only for you", L"Seulement pour vous", L"Solo para ti", L"Tylko dla Ciebie", L"Só para você", L"あなただけ", L"僅限你"}},
    {L"  ·  nicht gespeichert",
     {L"  ·  not saved", L"  ·  non enregistré", L"  ·  sin guardar", L"  ·  niezapisane", L"  ·  não salvo", L"  ·  未保存", L"  ·  尚未儲存"}},
    {L" Eintrag",
     {L" entry", L" élément", L" elemento", L" element", L" item", L" 件", L" 項"}},
    {L" Einträge",
     {L" entries", L" éléments", L" elementos", L" elementów", L" itens", L" 件", L" 項"}},
    {L"Optimieren",
     {L"Optimize", L"Optimiser", L"Optimizar", L"Optymalizuj", L"Otimizar", L"最適化", L"最佳化"}},
    {L"Änderungen übernehmen",
     {L"Apply changes", L"Appliquer les modifications", L"Aplicar cambios", L"Zastosuj zmiany", L"Aplicar alterações", L"変更を適用", L"套用變更"}},
    {L"Clean Now",
     {L"Clean Now", L"Nettoyer maintenant", L"Limpiar ahora", L"Wyczyść teraz", L"Limpar agora", L"今すぐクリーン", L"立即清理"}},
    {L"Alles sauber",
     {L"All clean", L"Tout est propre", L"Todo limpio", L"Wszystko czyste", L"Tudo limpo", L"すべてきれいです", L"全部乾淨"}},
    {L"Keine Browser-Caches gefunden.",
     {L"No browser caches found.", L"Aucun cache de navigateur trouvé.", L"No se encontraron cachés de navegador.", L"Nie znaleziono pamięci podręcznych przeglądarek.", L"Nenhum cache de navegador encontrado.", L"ブラウザーのキャッシュが見つかりません。", L"找不到瀏覽器快取。"}},
    {L"Chrome, Edge, Brave, Opera GX und Firefox sind aufgeräumt.",
     {L"Chrome, Edge, Brave, Opera GX and Firefox are tidy.", L"Chrome, Edge, Brave, Opera GX et Firefox sont nettoyés.", L"Chrome, Edge, Brave, Opera GX y Firefox están limpios.", L"Chrome, Edge, Brave, Opera GX i Firefox są uporządkowane.", L"Chrome, Edge, Brave, Opera GX e Firefox estão limpos.", L"Chrome、Edge、Brave、Opera GX、Firefox は整理済みです。", L"Chrome、Edge、Brave、Opera GX 和 Firefox 都已整理好。"}},
    {L"Dein Downloads-Ordner ist leer.",
     {L"Your Downloads folder is empty.", L"Votre dossier Téléchargements est vide.", L"Tu carpeta Descargas está vacía.", L"Twój folder Pobrane jest pusty.", L"Sua pasta Downloads está vazia.", L"ダウンロードフォルダーは空です。", L"你的「下載」資料夾是空的。"}},
    {L"Auto-Clean räumt ihn regelmäßig für dich auf.",
     {L"Auto-Clean tidies it for you regularly.", L"Auto-Clean le nettoie régulièrement pour vous.", L"Auto-Clean la limpia con regularidad por ti.", L"Auto-Clean regularnie go dla Ciebie porządkuje.", L"O Auto-Clean a limpa regularmente para você.", L"Auto-Clean が定期的に整理します。", L"Auto-Clean 會定期幫你整理。"}},
    {L"Auto-Clean ist aus – er wird nur von Hand geleert.",
     {L"Auto-Clean is off – it is only emptied manually.", L"Auto-Clean est désactivé – il n'est vidé qu'à la main.", L"Auto-Clean está desactivado – solo se vacía a mano.", L"Auto-Clean jest wyłączony – folder jest opróżniany tylko ręcznie.", L"O Auto-Clean está desativado – ela só é esvaziada manualmente.", L"Auto-Clean はオフです – 手動でのみ空にします。", L"Auto-Clean 已關閉 – 只會手動清空。"}},
    {L"Keine temporären Dateien gefunden.",
     {L"No temporary files found.", L"Aucun fichier temporaire trouvé.", L"No se encontraron archivos temporales.", L"Nie znaleziono plików tymczasowych.", L"Nenhum arquivo temporário encontrado.", L"一時ファイルが見つかりません。", L"找不到暫存檔案。"}},
    {L"Dein PC ist an dieser Stelle aufgeräumt.",
     {L"Your PC is tidy here.", L"Votre PC est propre à cet endroit.", L"Tu PC está limpio en este punto.", L"Twój komputer jest w tym miejscu uporządkowany.", L"Seu PC está limpo neste ponto.", L"この場所のPCはきれいです。", L"你的電腦在這裡很乾淨。"}},
    {L"Keine Vorschau-Caches oder Absturzberichte gefunden.",
     {L"No thumbnail caches or crash reports found.", L"Aucun cache de miniatures ni rapport de plantage trouvé.", L"No se encontraron cachés de miniaturas ni informes de fallos.", L"Nie znaleziono pamięci miniatur ani raportów awarii.", L"Nenhum cache de miniaturas ou relatório de falhas encontrado.", L"サムネイルキャッシュやクラッシュレポートが見つかりません。", L"找不到縮圖快取或當機報告。"}},
    {L"Hier gibt es nichts aufzuräumen.",
     {L"Nothing to clean up here.", L"Il n'y a rien à nettoyer ici.", L"Aquí no hay nada que limpiar.", L"Nie ma tu nic do sprzątania.", L"Não há nada para limpar aqui.", L"ここには整理するものがありません。", L"這裡沒有需要整理的項目。"}},
    {L"1 geschützter Eintrag bleibt erhalten.",
     {L"1 protected entry is kept.", L"1 élément protégé est conservé.", L"1 elemento protegido se conserva.", L"1 chroniony element zostaje zachowany.", L"1 item protegido é mantido.", L"保護された項目 1 件は保持されます。", L"1 個受保護的項目會保留。"}},
    {L" geschützte Einträge bleiben erhalten.",
     {L" protected entries are kept.", L" éléments protégés sont conservés.", L" elementos protegidos se conservan.", L" chronionych elementów zostaje zachowanych.", L" itens protegidos são mantidos.", L" 件の保護項目は保持されます。", L" 個受保護的項目會保留。"}},
    {L"Bereinige …  %d %%   (%d / %d)",
     {L"Cleaning …  %d %%   (%d / %d)", L"Nettoyage …  %d %%   (%d / %d)", L"Limpiando …  %d %%   (%d / %d)", L"Czyszczenie …  %d %%   (%d / %d)", L"Limpando …  %d %%   (%d / %d)", L"クリーン中 …  %d %%   (%d / %d)", L"清理中 …  %d %%   (%d / %d)"}},
    {L"Löscht: ",
     {L"Deleting: ", L"Suppression : ", L"Eliminando: ", L"Usuwanie: ", L"Excluindo: ", L"削除中： ", L"正在刪除： "}},
    {L"Gut",
     {L"Good", L"Bon", L"Bien", L"Dobrze", L"Bom", L"良好", L"良好"}},
    {L"Aufräumen",
     {L"Tidy up", L"À nettoyer", L"Limpiar", L"Do sprzątania", L"Limpar", L"整理推奨", L"建議整理"}},
    {L"Viel Ballast",
     {L"Lots of clutter", L"Très encombré", L"Mucho desorden", L"Dużo śmieci", L"Muita sujeira", L"不要ファイルが多い", L"雜物很多"}},
    {L"Analyse …",
     {L"Analyzing …", L"Analyse …", L"Analizando …", L"Analiza …", L"Analisando …", L"分析中 …", L"分析中 …"}},
    {L"Zustand",
     {L"Status", L"État", L"Estado", L"Stan", L"Estado", L"状態", L"狀態"}},
    {L"Analyse läuft …",
     {L"Analysis running …", L"Analyse en cours …", L"Análisis en curso …", L"Trwa analiza …", L"Análise em andamento …", L"分析中 …", L"分析進行中 …"}},
    {L"Analyse abgeschlossen  ·  ",
     {L"Analysis complete  ·  ", L"Analyse terminée  ·  ", L"Análisis completado  ·  ", L"Analiza zakończona  ·  ", L"Análise concluída  ·  ", L"分析完了  ·  ", L"分析完成  ·  "}},
    {L" Programme starten mit Windows  ·  ",
     {L" programs start with Windows  ·  ", L" programmes démarrent avec Windows  ·  ", L" programas se inician con Windows  ·  ", L" programów uruchamia się z Windows  ·  ", L" programas iniciam com o Windows  ·  ", L" 個のプログラムが Windows と同時に起動  ·  ", L" 個程式隨 Windows 啟動  ·  "}},
    {L" aktiv",
     {L" active", L" actifs", L" activos", L" aktywnych", L" ativos", L" 個が有効", L" 個已啟用"}},
    {L"Verhalten des Programms  ·  Änderungen gelten sofort",
     {L"Program behavior  ·  Changes apply immediately", L"Comportement du programme  ·  Les modifications s'appliquent immédiatement", L"Comportamiento del programa  ·  Los cambios se aplican al instante", L"Zachowanie programu  ·  Zmiany działają od razu", L"Comportamento do programa  ·  As alterações valem imediatamente", L"プログラムの動作  ·  変更はすぐに反映されます", L"程式行為  ·  變更會立即生效"}},
    {L" Einträge  ·  ",
     {L" entries  ·  ", L" éléments  ·  ", L" elementos  ·  ", L" elementów  ·  ", L" itens  ·  ", L" 件  ·  ", L" 項  ·  "}},
    {L"Analyse läuft …  ·  ",
     {L"Analysis running …  ·  ", L"Analyse en cours …  ·  ", L"Análisis en curso …  ·  ", L"Trwa analiza …  ·  ", L"Análise em andamento …  ·  ", L"分析中 …  ·  ", L"分析進行中 …  ·  "}},
    {L" geschützt",
     {L" protected", L" protégés", L" protegidos", L" chronionych", L" protegidos", L" 件が保護中", L" 項受保護"}},
    {L"Analyse läuft …  ·  nur Anzeige, es wird nichts verändert",
     {L"Analysis running …  ·  display only, nothing is changed", L"Analyse en cours …  ·  affichage seul, rien n'est modifié", L"Análisis en curso …  ·  solo visualización, no se cambia nada", L"Trwa analiza …  ·  tylko podgląd, nic nie jest zmieniane", L"Análise em andamento …  ·  somente exibição, nada é alterado", L"分析中 …  ·  表示のみ、何も変更しません", L"分析進行中 …  ·  僅供顯示，不會變更任何內容"}},
    {L" Laufwerke  ·  ",
     {L" drives  ·  ", L" lecteurs  ·  ", L" unidades  ·  ", L" dysków  ·  ", L" unidades  ·  ", L" ドライブ  ·  ", L" 個磁碟機  ·  "}},
    {L"Analyse läuft …  ·  nur Cache, nichts Persönliches",
     {L"Analysis running …  ·  cache only, nothing personal", L"Analyse en cours …  ·  cache uniquement, rien de personnel", L"Análisis en curso …  ·  solo caché, nada personal", L"Trwa analiza …  ·  tylko pamięć podręczna, nic osobistego", L"Análise em andamento …  ·  somente cache, nada pessoal", L"分析中 …  ·  キャッシュのみ、個人データはありません", L"分析進行中 …  ·  僅限快取，不含個人資料"}},
    {L" von 5 Browsern gefunden  ·  ",
     {L" of 5 browsers found  ·  ", L" sur 5 navigateurs trouvés  ·  ", L" de 5 navegadores encontrados  ·  ", L" z 5 przeglądarek znaleziono  ·  ", L" de 5 navegadores encontrados  ·  ", L" / 5 個のブラウザーを検出  ·  ", L" / 5 個瀏覽器已找到  ·  "}},
    {L" Cache  ·  wird endgültig gelöscht",
     {L" cache  ·  permanently deleted", L" de cache  ·  supprimé définitivement", L" de caché  ·  se elimina definitivamente", L" pamięci podręcznej  ·  usuwana na stałe", L" de cache  ·  excluído definitivamente", L" のキャッシュ  ·  完全に削除されます", L" 快取  ·  將永久刪除"}},
    {L"Update läuft – bitte das Fenster nicht schließen",
     {L"Update in progress – please do not close the window", L"Mise à jour en cours – ne fermez pas la fenêtre", L"Actualización en curso – no cierres la ventana", L"Trwa aktualizacja – nie zamykaj okna", L"Atualização em andamento – não feche a janela", L"更新中 – ウィンドウを閉じないでください", L"更新進行中 – 請勿關閉視窗"}},
    {L"Update lädt …  %d %%  ·  %.1f / %.1f MB",
     {L"Downloading update …  %d %%  ·  %.1f / %.1f MB", L"Téléchargement de la mise à jour …  %d %%  ·  %.1f / %.1f Mo", L"Descargando actualización …  %d %%  ·  %.1f / %.1f MB", L"Pobieranie aktualizacji …  %d %%  ·  %.1f / %.1f MB", L"Baixando atualização …  %d %%  ·  %.1f / %.1f MB", L"更新をダウンロード中 …  %d %%  ·  %.1f / %.1f MB", L"正在下載更新 …  %d %%  ·  %.1f / %.1f MB"}},
    {L"Update lädt …  %.1f MB",
     {L"Downloading update …  %.1f MB", L"Téléchargement de la mise à jour …  %.1f Mo", L"Descargando actualización …  %.1f MB", L"Pobieranie aktualizacji …  %.1f MB", L"Baixando atualização …  %.1f MB", L"更新をダウンロード中 …  %.1f MB", L"正在下載更新 …  %.1f MB"}},
    {L"Prüfe Signatur …",
     {L"Checking signature …", L"Vérification de la signature …", L"Comprobando firma …", L"Sprawdzanie podpisu …", L"Verificando assinatura …", L"署名を確認中 …", L"正在檢查簽章 …"}},
    {L"Installiere …",
     {L"Installing …", L"Installation …", L"Instalando …", L"Instalowanie …", L"Instalando …", L"インストール中 …", L"安裝中 …"}},
    {L"Bereinigung läuft …",
     {L"Cleaning in progress …", L"Nettoyage en cours …", L"Limpieza en curso …", L"Trwa czyszczenie …", L"Limpeza em andamento …", L"クリーン中 …", L"清理進行中 …"}},
    {L" Objekte (",
     {L" objects (", L" objets (", L" objetos (", L" obiektów (", L" objetos (", L" 個のオブジェクト (", L" 個物件 ("}},
    {L") gefunden",
     {L") found", L") trouvés", L") encontrados", L") znaleziono", L") encontrados", L") を検出", L") 已找到"}},
    {L"Mit „Optimieren“ entfernbar: ",
     {L"Removable with \"Optimize\": ", L"Supprimable avec « Optimiser » : ", L"Se puede eliminar con «Optimizar»: ", L"Do usunięcia przez „Optymalizuj”: ", L"Removível com «Otimizar»: ", L"「最適化」で削除できる量： ", L"可用「最佳化」移除： "}},
    {L" (ohne Browser-Cache)",
     {L" (excluding browser cache)", L" (hors cache des navigateurs)", L" (sin la caché del navegador)", L" (bez pamięci podręcznej przeglądarek)", L" (sem o cache do navegador)", L" (ブラウザーのキャッシュを除く)", L" (不含瀏覽器快取)"}},
    {L"Details (Admin)",
     {L"Details (Admin)", L"Détails (admin)", L"Detalles (admin)", L"Szczegóły (admin)", L"Detalhes (admin)", L"詳細 (管理者)", L"詳細資料 (系統管理員)"}},
    {L"mehr Werte mit „Details (Admin)“",
     {L"more values with \"Details (Admin)\"", L"plus de valeurs avec « Détails (admin) »", L"más valores con «Detalles (admin)»", L"więcej wartości po „Szczegóły (admin)”", L"mais valores com «Detalhes (admin)»", L"「詳細 (管理者)」でさらに値を表示", L"使用「詳細資料 (系統管理員)」可看到更多數值"}},
    {L"Windows meldet keine weiteren Werte",
     {L"Windows reports no further values", L"Windows ne signale aucune autre valeur", L"Windows no informa de más valores", L"Windows nie podaje dalszych wartości", L"O Windows não informa outros valores", L"Windows はこれ以上の値を報告していません", L"Windows 未回報其他數值"}},
    {L"Windows fragt gleich nach Administrator-Rechten. Damit liest das Programm nur die Gesundheitswerte deiner Laufwerke (Temperatur, Abnutzung, Betriebsstunden, Fehler). Es wird nichts verändert oder gelöscht.",
     {L"Windows will now ask for administrator rights. The program only uses them to read the health values of your drives (temperature, wear, power-on hours, errors). Nothing is changed or deleted.", L"Windows va demander les droits d'administrateur. Le programme les utilise uniquement pour lire les valeurs de santé de vos lecteurs (température, usure, heures de fonctionnement, erreurs). Rien n'est modifié ni supprimé.", L"Windows pedirá ahora derechos de administrador. El programa solo los usa para leer los valores de salud de tus unidades (temperatura, desgaste, horas de funcionamiento, errores). No se cambia ni se elimina nada.", L"Windows zaraz poprosi o uprawnienia administratora. Program używa ich tylko do odczytu wartości stanu Twoich dysków (temperatura, zużycie, godziny pracy, błędy). Nic nie jest zmieniane ani usuwane.", L"O Windows vai pedir direitos de administrador. O programa os usa apenas para ler os valores de integridade das suas unidades (temperatura, desgaste, horas de funcionamento, erros). Nada é alterado ou excluído.", L"Windows が管理者権限を求めます。プログラムはドライブの正常性の値 (温度、消耗、稼働時間、エラー) を読み取るためだけに使用します。何も変更・削除されません。", L"Windows 接下來會要求系統管理員權限。程式只用它來讀取磁碟機的健康狀態值 (溫度、損耗、運作時數、錯誤)。不會變更或刪除任何內容。"}},
    {L"Die Werte konnten nicht gelesen werden (Abfrage abgebrochen oder von Windows nicht gemeldet).",
     {L"The values could not be read (request cancelled or not reported by Windows).", L"Impossible de lire les valeurs (demande annulée ou non fournies par Windows).", L"No se pudieron leer los valores (consulta cancelada o no informada por Windows).", L"Nie udało się odczytać wartości (zapytanie anulowane lub niepodane przez Windows).", L"Não foi possível ler os valores (consulta cancelada ou não informada pelo Windows).", L"値を読み取れませんでした (問い合わせがキャンセルされたか、Windows が報告していません)。", L"無法讀取數值 (查詢已取消，或 Windows 未回報)。"}},
    {L"Temp & System: An",
     {L"Temp & System: On", L"Temp & Système : oui", L"Temp y Sistema: sí", L"Temp i System: tak", L"Temp e Sistema: sim", L"Temp & システム：オン", L"Temp 與系統：開"}},
    {L"Temp & System: Aus",
     {L"Temp & System: Off", L"Temp & Système : non", L"Temp y Sistema: no", L"Temp i System: nie", L"Temp e Sistema: não", L"Temp & システム：オフ", L"Temp 與系統：關"}},
    {L"Beim automatischen Aufräumen werden zusätzlich diese Bereiche geleert:\n\n • Temp-Dateien (AppData\\Local\\Temp)\n • Vorschaubild- und Symbol-Cache des Explorers\n • Absturzabbilder (CrashDumps)\n • Windows-Fehlerberichte\n\nDas wird ENDGÜLTIG gelöscht, nicht in den Papierkorb. Dateien, die gerade benutzt werden, und Einträge der Schutzliste bleiben erhalten.\n\nEinschalten?",
     {L"During automatic cleanup these areas are also emptied:\n\n • Temp files (AppData\\Local\\Temp)\n • Explorer thumbnail and icon cache\n • Crash dumps (CrashDumps)\n • Windows error reports\n\nThis is deleted PERMANENTLY, not moved to the Recycle Bin. Files currently in use and entries on the protected list are kept.\n\nTurn on?", L"Lors du nettoyage automatique, ces zones sont également vidées :\n\n • Fichiers temporaires (AppData\\Local\\Temp)\n • Cache des miniatures et des icônes de l'Explorateur\n • Fichiers de vidage sur incident (CrashDumps)\n • Rapports d'erreurs Windows\n\nLa suppression est DÉFINITIVE, sans passer par la Corbeille. Les fichiers en cours d'utilisation et les entrées de la liste de protection sont conservés.\n\nActiver ?", L"En la limpieza automática también se vacían estas áreas:\n\n • Archivos temporales (AppData\\Local\\Temp)\n • Caché de miniaturas e iconos del Explorador\n • Volcados de bloqueo (CrashDumps)\n • Informes de errores de Windows\n\nSe elimina DEFINITIVAMENTE, sin pasar por la Papelera. Los archivos en uso y las entradas de la lista de protección se conservan.\n\n¿Activar?", L"Podczas automatycznego sprzątania opróżniane są dodatkowo te obszary:\n\n • Pliki tymczasowe (AppData\\Local\\Temp)\n • Pamięć miniatur i ikon Eksploratora\n • Zrzuty awarii (CrashDumps)\n • Raporty błędów Windows\n\nUsuwane jest NA STAŁE, z pominięciem Kosza. Używane pliki i wpisy listy ochrony zostają zachowane.\n\nWłączyć?", L"Na limpeza automática, estas áreas também são esvaziadas:\n\n • Arquivos temporários (AppData\\Local\\Temp)\n • Cache de miniaturas e ícones do Explorer\n • Despejos de falha (CrashDumps)\n • Relatórios de erros do Windows\n\nA exclusão é DEFINITIVA, sem passar pela Lixeira. Arquivos em uso e entradas da lista de proteção são mantidos.\n\nAtivar?", L"自動クリーンでは、次の領域も空にします：\n\n • 一時ファイル (AppData\\Local\\Temp)\n • エクスプローラーのサムネイル・アイコンのキャッシュ\n • クラッシュダンプ (CrashDumps)\n • Windows のエラーレポート\n\nごみ箱を経由せず、完全に削除されます。使用中のファイルと保護リストの項目は保持されます。\n\n有効にしますか？", L"自動清理時，也會清空以下區域：\n\n • 暫存檔案 (AppData\\Local\\Temp)\n • 檔案總管的縮圖與圖示快取\n • 當機傾印檔 (CrashDumps)\n • Windows 錯誤報告\n\n這些內容會「永久」刪除，不會進入資源回收筒。使用中的檔案與保護清單中的項目會保留。\n\n要開啟嗎？"}},
    {L"Browser laufen noch",
     {L"Browsers still running", L"Navigateurs encore ouverts", L"Navegadores aún abiertos", L"Przeglądarki nadal działają", L"Navegadores ainda abertos", L"ブラウザーがまだ起動中です", L"瀏覽器仍在執行"}},
    {L"Browser schließen",
     {L"Close browsers", L"Fermer les navigateurs", L"Cerrar navegadores", L"Zamknij przeglądarki", L"Fechar navegadores", L"ブラウザーを閉じる", L"關閉瀏覽器"}},
    {L"Ohne Schließen fortfahren",
     {L"Continue without closing", L"Continuer sans fermer", L"Continuar sin cerrar", L"Kontynuuj bez zamykania", L"Continuar sem fechar", L"閉じずに続行", L"不關閉，繼續"}},
    {L"Abbrechen",
     {L"Cancel", L"Annuler", L"Cancelar", L"Anuluj", L"Cancelar", L"キャンセル", L"取消"}},
    {L"Laufende Browser sperren ihre Cache-Dateien. Jetzt schließen, damit der Cache komplett geleert werden kann?\n\nUngespeicherte Eingaben auf Webseiten können dabei verloren gehen. Ohne Schließen bleiben benutzte Dateien erhalten.",
     {L"Running browsers lock their cache files. Close them now so the cache can be cleared completely?\n\nUnsaved input on web pages may be lost. Without closing, files in use are kept.", L"Les navigateurs ouverts verrouillent leurs fichiers de cache. Les fermer maintenant pour vider complètement le cache ?\n\nLes saisies non enregistrées sur des pages web peuvent être perdues. Sans fermeture, les fichiers utilisés sont conservés.", L"Los navegadores abiertos bloquean sus archivos de caché. ¿Cerrarlos ahora para vaciar la caché por completo?\n\nLo que no hayas guardado en páginas web puede perderse. Sin cerrar, los archivos en uso se conservan.", L"Działające przeglądarki blokują swoje pliki pamięci podręcznej. Zamknąć je teraz, aby całkowicie opróżnić pamięć podręczną?\n\nNiezapisane dane na stronach internetowych mogą zostać utracone. Bez zamykania używane pliki zostają zachowane.", L"Navegadores abertos bloqueiam seus arquivos de cache. Fechá-los agora para esvaziar o cache por completo?\n\nO que não foi salvo em páginas da web pode ser perdido. Sem fechar, os arquivos em uso são mantidos.", L"起動中のブラウザーはキャッシュファイルをロックしています。キャッシュを完全に空にするため、今すぐ閉じますか？\n\nWeb ページで未保存の入力は失われる可能性があります。閉じない場合、使用中のファイルは残ります。", L"執行中的瀏覽器會鎖定其快取檔案。要現在關閉它們，才能完整清空快取嗎？\n\n網頁上尚未儲存的輸入內容可能會遺失。若不關閉，使用中的檔案會保留。"}},
    {L"Jetzt schnell reinigen",
     {L"Quick clean now", L"Nettoyage rapide", L"Limpieza rápida", L"Szybkie czyszczenie", L"Limpeza rápida", L"クイッククリーン", L"快速清理"}},
    {L"Schnell-Reinigung fertig",
     {L"Quick clean finished", L"Nettoyage rapide terminé", L"Limpieza rápida terminada", L"Szybkie czyszczenie zakończone", L"Limpeza rápida concluída", L"クイッククリーンが完了しました", L"快速清理完成"}},
    {L"%s freigegeben (%d Einträge)",
     {L"%s freed (%d entries)", L"%s libérés (%d éléments)", L"%s liberados (%d elementos)", L"Zwolniono %s (%d elementów)", L"%s liberados (%d itens)", L"%s を解放しました (%d 件)", L"已釋放 %s (%d 項)"}},
    {L"Ja",
     {L"Yes", L"Oui", L"Sí", L"Tak", L"Sim", L"はい", L"是"}},
    {L"Nein",
     {L"No", L"Non", L"No", L"Nie", L"Não", L"いいえ", L"否"}},
    {L"Aus Downloads wählen",
     {L"Choose from Downloads", L"Choisir dans Téléchargements", L"Elegir de Descargas", L"Wybierz z Pobranych", L"Escolher em Downloads", L"ダウンロードから選択", L"從「下載」選擇"}},
    {L" Datei",
     {L" file", L" fichier", L" archivo", L" plik", L" arquivo", L" ファイル", L" 個檔案"}},
    {L" Dateien",
     {L" files", L" fichiers", L" archivos", L" plików", L" arquivos", L" ファイル", L" 個檔案"}},
    {L"Ein Klick auf eine Datei übernimmt ihren Namen in die Schutzliste.",
     {L"A click on a file adds its name to the protected list.", L"Un clic sur un fichier ajoute son nom à la liste de protection.", L"Un clic en un archivo añade su nombre a la lista de protección.", L"Kliknięcie pliku dodaje jego nazwę do listy ochrony.", L"Um clique em um arquivo adiciona o nome dele à lista de proteção.", L"ファイルをクリックすると、その名前が保護リストに追加されます。", L"點一下檔案，就會把它的名稱加入保護清單。"}},
    {L"Keine Dateien in Downloads.",
     {L"No files in Downloads.", L"Aucun fichier dans Téléchargements.", L"No hay archivos en Descargas.", L"Brak plików w Pobranych.", L"Nenhum arquivo em Downloads.", L"ダウンロードにファイルがありません。", L"「下載」中沒有檔案。"}},
    {L"Niedrig",
     {L"Low", L"Faible", L"Bajo", L"Niski", L"Baixo", L"低", L"低"}},
    {L"Mittel",
     {L"Medium", L"Moyen", L"Medio", L"Średni", L"Médio", L"中", L"中"}},
    {L"Hoch",
     {L"High", L"Élevé", L"Alto", L"Wysoki", L"Alto", L"高", L"高"}},
    {L"Pfad:  ",
     {L"Path:  ", L"Chemin :  ", L"Ruta:  ", L"Ścieżka:  ", L"Caminho:  ", L"パス：  ", L"路徑：  "}},
    {L"Herausgeber:  ",
     {L"Publisher:  ", L"Éditeur :  ", L"Editor:  ", L"Wydawca:  ", L"Editor:  ", L"発行元：  ", L"發行者：  "}},
    {L"Unbekannt",
     {L"Unknown", L"Inconnu", L"Desconocido", L"Nieznany", L"Desconhecido", L"不明", L"未知"}},
    {L"Startauswirkung: ",
     {L"Startup impact: ", L"Impact au démarrage : ", L"Impacto en el inicio: ", L"Wpływ na uruchamianie: ", L"Impacto na inicialização: ", L"起動への影響： ", L"啟動影響： "}},
    {L" (Schätzung nach Programmgröße)",
     {L" (estimate based on program size)", L" (estimation selon la taille du programme)", L" (estimación según el tamaño del programa)", L" (szacunek według rozmiaru programu)", L" (estimativa pelo tamanho do programa)", L" (プログラムサイズによる推定)", L" (依程式大小估計)"}},
    {L"Alle %d Tage",
     {L"Every %d days", L"Tous les %d jours", L"Cada %d días", L"Co %d dni", L"A cada %d dias", L"%d 日ごと", L"每 %d 天"}},
    {L"Behalten: nichts",
     {L"Keep: nothing", L"Conserver : rien", L"Conservar: nada", L"Zachowaj: nic", L"Manter: nada", L"保持：なし", L"保留：無"}},
    {L"Behalten: < %d MB",
     {L"Keep: < %d MB", L"Conserver : < %d Mo", L"Conservar: < %d MB", L"Zachowaj: < %d MB", L"Manter: < %d MB", L"保持：< %d MB", L"保留：< %d MB"}},
    {L"Kleiner als %d MB bleibt erhalten",
     {L"Smaller than %d MB is kept", L"Moins de %d Mo est conservé", L"Lo que ocupe menos de %d MB se conserva", L"Mniejsze niż %d MB zostaje zachowane", L"Menos de %d MB é mantido", L"%d MB 未満は保持されます", L"小於 %d MB 的會保留"}},
    {L"Keine Ausnahme: alles wird aufgeräumt",
     {L"No exception: everything is tidied", L"Aucune exception : tout est nettoyé", L"Sin excepción: se limpia todo", L"Bez wyjątków: wszystko jest sprzątane", L"Sem exceção: tudo é limpo", L"例外なし：すべて整理します", L"無例外：全部整理"}},
    {L"Programm nicht gefunden  ·  ",
     {L"Program not found  ·  ", L"Programme introuvable  ·  ", L"Programa no encontrado  ·  ", L"Nie znaleziono programu  ·  ", L"Programa não encontrado  ·  ", L"プログラムが見つかりません  ·  ", L"找不到程式  ·  "}},
    {L"Bitte nur einen Dateinamen oder ein Muster eintragen, keinen Pfad (z. B. Rechnung.pdf oder *.pdf).",
     {L"Please enter only a file name or a pattern, no path (e.g. invoice.pdf or *.pdf).", L"Veuillez saisir uniquement un nom de fichier ou un motif, pas un chemin (p. ex. Facture.pdf ou *.pdf).", L"Introduce solo un nombre de archivo o un patrón, no una ruta (p. ej. Factura.pdf o *.pdf).", L"Wpisz tylko nazwę pliku lub wzorzec, bez ścieżki (np. Faktura.pdf lub *.pdf).", L"Informe apenas um nome de arquivo ou um padrão, sem caminho (p. ex. Fatura.pdf ou *.pdf).", L"ファイル名またはパターンだけを入力してください。パスは不可です (例：請求書.pdf や *.pdf)。", L"請只輸入檔名或比對模式，不要輸入路徑 (例如 發票.pdf 或 *.pdf)。"}},
    {L"Noch nichts geschützt",
     {L"Nothing protected yet", L"Rien de protégé pour l'instant", L"Aún no hay nada protegido", L"Nic jeszcze nie chronisz", L"Nada protegido ainda", L"まだ何も保護していません", L"尚未保護任何項目"}},
    {L"Trage oben einen Dateinamen oder ein Muster ein.",
     {L"Enter a file name or a pattern above.", L"Saisissez ci-dessus un nom de fichier ou un motif.", L"Introduce arriba un nombre de archivo o un patrón.", L"Wpisz powyżej nazwę pliku lub wzorzec.", L"Digite acima um nome de arquivo ou um padrão.", L"上にファイル名またはパターンを入力してください。", L"請在上方輸入檔名或比對模式。"}},
    {L"Dateiname oder Muster, z. B. *.pdf",
     {L"File name or pattern, e.g. *.pdf", L"Nom de fichier ou motif, p. ex. *.pdf", L"Nombre de archivo o patrón, p. ej. *.pdf", L"Nazwa pliku lub wzorzec, np. *.pdf", L"Nome de arquivo ou padrão, p. ex. *.pdf", L"ファイル名またはパターン (例： *.pdf)", L"檔名或比對模式，例如 *.pdf"}},
    {L"Hinzufügen",
     {L"Add", L"Ajouter", L"Añadir", L"Dodaj", L"Adicionar", L"追加", L"新增"}},
    {L"Aus Downloads …",
     {L"From Downloads …", L"Depuis Téléchargements …", L"Desde Descargas …", L"Z Pobranych …", L"De Downloads …", L"ダウンロードから …", L"從「下載」…"}},
    {L"  ·  werden nie gelöscht",
     {L"  ·  are never deleted", L"  ·  ne sont jamais supprimés", L"  ·  nunca se eliminan", L"  ·  nigdy nie są usuwane", L"  ·  nunca são excluídos", L"  ·  は削除されません", L"  ·  永遠不會被刪除"}},
    {L"Gilt in allen Bereichen. Platzhalter: * (z. B. *.pdf oder Rechnung*)",
     {L"Applies everywhere. Wildcard: * (e.g. *.pdf or invoice*)", L"S'applique partout. Joker : * (p. ex. *.pdf ou Facture*)", L"Vale en todas las áreas. Comodín: * (p. ej. *.pdf o Factura*)", L"Obowiązuje wszędzie. Symbol wieloznaczny: * (np. *.pdf lub Faktura*)", L"Vale em todas as áreas. Curinga: * (p. ex. *.pdf ou Fatura*)", L"すべての領域に適用されます。ワイルドカード： * (例： *.pdf や 請求書*)", L"適用於所有區域。萬用字元：* (例如 *.pdf 或 發票*)"}},
    {L"Immer geschützt: desktop.ini, Thumbs.db, Systemdateien und das Programm selbst.",
     {L"Always protected: desktop.ini, Thumbs.db, system files and the program itself.", L"Toujours protégés : desktop.ini, Thumbs.db, fichiers système et le programme lui-même.", L"Siempre protegidos: desktop.ini, Thumbs.db, archivos del sistema y el propio programa.", L"Zawsze chronione: desktop.ini, Thumbs.db, pliki systemowe i sam program.", L"Sempre protegidos: desktop.ini, Thumbs.db, arquivos do sistema e o próprio programa.", L"常に保護：desktop.ini、Thumbs.db、システムファイル、プログラム自身。", L"永遠受保護：desktop.ini、Thumbs.db、系統檔案與程式本身。"}},
    {L"Duncan's Tidy wird geladen …",
     {L"Duncan's Tidy is loading …", L"Chargement de Duncan's Tidy …", L"Cargando Duncan's Tidy …", L"Ładowanie Duncan's Tidy …", L"Carregando o Duncan's Tidy …", L"Duncan's Tidy を読み込み中 …", L"正在載入 Duncan's Tidy …"}},
    {L"Größte Dateien auf ",
     {L"Largest files on ", L"Plus gros fichiers sur ", L"Archivos más grandes en ", L"Największe pliki na ", L"Maiores arquivos em ", L"最大のファイル： ", L"最大的檔案，位於 "}},
    {L"Die 20 größten  ·  zusammen ",
     {L"The 20 largest  ·  total ", L"Les 20 plus gros  ·  total ", L"Los 20 mayores  ·  total ", L"20 największych  ·  razem ", L"Os 20 maiores  ·  total ", L"上位 20 件  ·  合計 ", L"最大的 20 個  ·  合計 "}},
    {L"Keine Dateien gefunden.",
     {L"No files found.", L"Aucun fichier trouvé.", L"No se encontraron archivos.", L"Nie znaleziono plików.", L"Nenhum arquivo encontrado.", L"ファイルが見つかりません。", L"找不到檔案。"}},
    {L"Suche größte Dateien auf ",
     {L"Searching largest files on ", L"Recherche des plus gros fichiers sur ", L"Buscando los archivos más grandes en ", L"Szukanie największych plików na ", L"Procurando os maiores arquivos em ", L"最大のファイルを検索中： ", L"正在搜尋最大的檔案，位於 "}},
    {L"Klick: größte Dateien",
     {L"Click: largest files", L"Clic : plus gros fichiers", L"Clic: archivos más grandes", L"Kliknij: największe pliki", L"Clique: maiores arquivos", L"クリック：最大のファイル", L"點一下：最大的檔案"}},
    {L"Klick auf einen Eintrag zeigt die Datei im Explorer. Es wird nichts gelöscht.",
     {L"Click an entry to show the file in Explorer. Nothing is deleted.", L"Un clic sur une entrée affiche le fichier dans l'Explorateur. Rien n'est supprimé.", L"Un clic en una entrada muestra el archivo en el Explorador. No se elimina nada.", L"Kliknięcie wpisu pokazuje plik w Eksploratorze. Nic nie jest usuwane.", L"Um clique em uma entrada mostra o arquivo no Explorer. Nada é excluído.", L"項目をクリックすると、エクスプローラーでファイルを表示します。何も削除されません。", L"點一下項目，會在檔案總管中顯示該檔案。不會刪除任何內容。"}},
    {L"Sicherer Modus: Nur dein Benutzerkonto, benutzte Dateien bleiben erhalten.",
     {L"Safe mode: only your user account, files in use are kept.", L"Mode sécurisé : uniquement votre compte utilisateur, les fichiers utilisés sont conservés.", L"Modo seguro: solo tu cuenta de usuario, los archivos en uso se conservan.", L"Tryb bezpieczny: tylko Twoje konto użytkownika, używane pliki zostają zachowane.", L"Modo seguro: somente sua conta de usuário, arquivos em uso são mantidos.", L"セーフモード：あなたのユーザーアカウントのみ。使用中のファイルは保持されます。", L"安全模式：僅限你的使用者帳戶，使用中的檔案會保留。"}},
    {L"Bitte einen Moment warten",
     {L"Please wait a moment", L"Veuillez patienter un instant", L"Espera un momento", L"Chwileczkę", L"Aguarde um momento", L"しばらくお待ちください", L"請稍候"}},
    {L"Laufwerke",
     {L"Drives", L"Lecteurs", L"Unidades", L"Dyski", L"Unidades", L"ドライブ", L"磁碟機"}},
    {L"Belegung und Gesundheit",
     {L"Usage and health", L"Occupation et état de santé", L"Ocupación y salud", L"Zajętość i stan", L"Ocupação e integridade", L"使用状況と正常性", L"使用量與健康狀態"}},
    {L" frei",
     {L" free", L" libres", L" libres", L" wolne", L" livres", L" 空き", L" 可用"}},
    {L"Autostart",
     {L"Autostart", L"Démarrage", L"Inicio automático", L"Autostart", L"Inicialização", L"スタートアップ", L"開機啟動"}},
    {L"Programme mit Windows",
     {L"Programs with Windows", L"Programmes avec Windows", L"Programas con Windows", L"Programy z Windows", L"Programas com o Windows", L"Windows と一緒に起動するプログラム", L"隨 Windows 啟動的程式"}},
    {L"von ",
     {L"of ", L"sur ", L"de ", L"z ", L"de ", L"全 ", L"共 "}},
    {L"Browser auswählen",
     {L"Select browsers", L"Choisir les navigateurs", L"Elegir navegadores", L"Wybierz przeglądarki", L"Escolher navegadores", L"ブラウザーを選択", L"選擇瀏覽器"}},
    {L"Auswahl",
     {L"Selection", L"Sélection", L"Selección", L"Wybór", L"Seleção", L"選択", L"選取"}},
    {L"Die Analyse läuft noch. Bitte einen Moment warten.",
     {L"The analysis is still running. Please wait a moment.", L"L'analyse est encore en cours. Veuillez patienter un instant.", L"El análisis aún está en curso. Espera un momento.", L"Analiza jeszcze trwa. Chwileczkę.", L"A análise ainda está em andamento. Aguarde um momento.", L"分析がまだ実行中です。しばらくお待ちください。", L"分析仍在進行中。請稍候。"}},
    {L"Es gibt nichts zu bereinigen.",
     {L"There is nothing to clean.", L"Il n'y a rien à nettoyer.", L"No hay nada que limpiar.", L"Nie ma nic do wyczyszczenia.", L"Não há nada para limpar.", L"クリーンするものがありません。", L"沒有需要清理的項目。"}},
    {L"Folgendes wird bereinigt:\n\n • Downloads: ",
     {L"The following will be cleaned:\n\n • Downloads: ", L"Les éléments suivants seront nettoyés :\n\n • Téléchargements : ", L"Se limpiará lo siguiente:\n\n • Descargas: ", L"Zostanie wyczyszczone:\n\n • Pobrane: ", L"Será limpo o seguinte:\n\n • Downloads: ", L"次の項目をクリーンします：\n\n • ダウンロード： ", L"將清理以下項目：\n\n • 下載： "}},
    {L") – landen im Papierkorb\n • Temp-Dateien: ",
     {L") – moved to the Recycle Bin\n • Temp files: ", L") – placés dans la Corbeille\n • Fichiers temporaires : ", L") – van a la Papelera\n • Archivos temporales: ", L") – trafią do Kosza\n • Pliki tymczasowe: ", L") – vão para a Lixeira\n • Arquivos temporários: ", L") – ごみ箱に移動します\n • 一時ファイル： ", L") – 會移到資源回收筒\n • 暫存檔案： "}},
    {L") – endgültig\n • Systemdateien: ",
     {L") – permanently\n • System files: ", L") – définitivement\n • Fichiers système : ", L") – definitivamente\n • Archivos del sistema: ", L") – na stałe\n • Pliki systemowe: ", L") – definitivamente\n • Arquivos do sistema: ", L") – 完全に削除します\n • システムファイル： ", L") – 永久刪除\n • 系統檔案： "}},
    {L") – endgültig\n\nGeschützte Dateien (Schutzliste) bleiben erhalten. Temp- und Systemdateien lassen sich nicht wiederherstellen.",
     {L") – permanently\n\nProtected files (protected list) are kept. Temp and system files cannot be restored.", L") – définitivement\n\nLes fichiers protégés (liste de protection) sont conservés. Les fichiers temporaires et système ne peuvent pas être restaurés.", L") – definitivamente\n\nLos archivos protegidos (lista de protección) se conservan. Los archivos temporales y del sistema no se pueden restaurar.", L") – na stałe\n\nChronione pliki (lista ochrony) zostają zachowane. Plików tymczasowych i systemowych nie można przywrócić.", L") – definitivamente\n\nArquivos protegidos (lista de proteção) são mantidos. Arquivos temporários e do sistema não podem ser restaurados.", L") – 完全に削除します\n\n保護されたファイル (保護リスト) は保持されます。一時ファイルとシステムファイルは元に戻せません。", L") – 永久刪除\n\n受保護的檔案 (保護清單) 會保留。暫存檔案與系統檔案無法還原。"}},
    {L" Änderung(en) gespeichert. Sie gelten ab dem nächsten Start von Windows.",
     {L" change(s) saved. They take effect at the next Windows start.", L" modification(s) enregistrée(s). Elles s'appliquent au prochain démarrage de Windows.", L" cambio(s) guardado(s). Se aplican en el próximo inicio de Windows.", L" zmian zapisano. Zaczną działać od następnego uruchomienia Windows.", L" alteração(ões) salva(s). Valem a partir da próxima inicialização do Windows.", L" 件の変更を保存しました。Windows の次回起動から有効になります。", L" 項變更已儲存。將在下次啟動 Windows 時生效。"}},
    {L"Es gab keine Änderungen.",
     {L"There were no changes.", L"Il n'y a eu aucune modification.", L"No hubo cambios.", L"Nie było zmian.", L"Não houve alterações.", L"変更はありませんでした。", L"沒有任何變更。"}},
    {L" Eintrag/Einträge konnten nicht geändert werden.",
     {L" entry/entries could not be changed.", L" élément(s) n'ont pas pu être modifiés.", L" elemento(s) no se pudieron cambiar.", L" wpisów nie udało się zmienić.", L" item(ns) não puderam ser alterados.", L" 件の項目を変更できませんでした。", L" 個項目無法變更。"}},
    {L"Es ist kein Browser ausgewählt.",
     {L"No browser is selected.", L"Aucun navigateur n'est sélectionné.", L"No hay ningún navegador seleccionado.", L"Nie wybrano żadnej przeglądarki.", L"Nenhum navegador selecionado.", L"ブラウザーが選択されていません。", L"尚未選取任何瀏覽器。"}},
    {L"Der Cache folgender Browser wird ENDGÜLTIG gelöscht:\n",
     {L"The cache of the following browsers will be PERMANENTLY deleted:\n", L"Le cache des navigateurs suivants sera supprimé DÉFINITIVEMENT :\n", L"La caché de estos navegadores se eliminará DEFINITIVAMENTE:\n", L"Pamięć podręczna tych przeglądarek zostanie usunięta NA STAŁE:\n", L"O cache dos seguintes navegadores será excluído DEFINITIVAMENTE:\n", L"次のブラウザーのキャッシュを完全に削除します：\n", L"以下瀏覽器的快取將被「永久」刪除：\n"}},
    {L"\n\nCookies, Passwörter, Verlauf und Lesezeichen bleiben erhalten.",
     {L"\n\nCookies, passwords, history and bookmarks are kept.", L"\n\nLes cookies, mots de passe, historique et favoris sont conservés.", L"\n\nLas cookies, contraseñas, historial y marcadores se conservan.", L"\n\nCiasteczka, hasła, historia i zakładki zostają zachowane.", L"\n\nCookies, senhas, histórico e favoritos são mantidos.", L"\n\nCookie、パスワード、履歴、ブックマークは保持されます。", L"\n\nCookie、密碼、瀏覽記錄與書籤會保留。"}},
    {L"\n\nHinweis: ",
     {L"\n\nNote: ", L"\n\nRemarque : ", L"\n\nNota: ", L"\n\nUwaga: ", L"\n\nObservação: ", L"\n\n注意： ", L"\n\n注意： "}},
    {L" läuft noch. Benutzte Dateien bleiben dann erhalten; am besten vorher schließen.",
     {L" is still running. Files in use are kept then; it is best to close it first.", L" est encore ouvert. Les fichiers utilisés sont alors conservés ; mieux vaut le fermer avant.", L" sigue en ejecución. Los archivos en uso se conservan; lo mejor es cerrarlo antes.", L" nadal działa. Używane pliki zostaną wtedy zachowane; najlepiej wcześniej zamknąć.", L" ainda está em execução. Os arquivos em uso serão mantidos; o melhor é fechá-lo antes.", L" はまだ実行中です。使用中のファイルは残ります。先に閉じることをおすすめします。", L" 仍在執行。使用中的檔案會保留；最好先關閉它。"}},
    {L"Es ist nichts ausgewählt.",
     {L"Nothing is selected.", L"Rien n'est sélectionné.", L"No hay nada seleccionado.", L"Nic nie wybrano.", L"Nada selecionado.", L"何も選択されていません。", L"尚未選取任何項目。"}},
    {L"%d von %d Einträgen in den Papierkorb verschieben?",
     {L"Move %d of %d entries to the Recycle Bin?", L"Déplacer %d éléments sur %d vers la Corbeille ?", L"¿Mover %d de %d elementos a la Papelera?", L"Przenieść %d z %d elementów do Kosza?", L"Mover %d de %d itens para a Lixeira?", L"%d 件中 %d 件をごみ箱に移動しますか？", L"要將 %d / %d 個項目移到資源回收筒嗎？"}},
    {L"%d von %d Einträgen ENDGÜLTIG löschen? (nicht wiederherstellbar)",
     {L"Permanently delete %d of %d entries? (cannot be restored)", L"Supprimer DÉFINITIVEMENT %d éléments sur %d ? (irréversible)", L"¿Eliminar DEFINITIVAMENTE %d de %d elementos? (no se puede deshacer)", L"Usunąć NA STAŁE %d z %d elementów? (nie można przywrócić)", L"Excluir DEFINITIVAMENTE %d de %d itens? (não pode ser desfeito)", L"%d 件中 %d 件を完全に削除しますか？ (元に戻せません)", L"要「永久」刪除 %d / %d 個項目嗎？ (無法還原)"}},
    {L"Dateien in deinem Downloads-Ordner. Beim Löschen landen sie im Papierkorb.\nKlick für die genaue Übersicht.",
     {L"Files in your Downloads folder. When deleted they go to the Recycle Bin.\nClick for details.", L"Fichiers de votre dossier Téléchargements. Supprimés, ils vont dans la Corbeille.\nClic pour l'aperçu détaillé.", L"Archivos de tu carpeta Descargas. Al eliminarlos van a la Papelera.\nClic para ver el detalle.", L"Pliki w Twoim folderze Pobrane. Po usunięciu trafiają do Kosza.\nKliknij, aby zobaczyć szczegóły.", L"Arquivos da sua pasta Downloads. Ao excluir, vão para a Lixeira.\nClique para ver os detalhes.", L"ダウンロードフォルダーのファイルです。削除するとごみ箱に入ります。\nクリックで詳細を表示。", L"你「下載」資料夾中的檔案。刪除後會進入資源回收筒。\n點一下可查看詳細資訊。"}},
    {L"Temporäre Dateien von Programmen. Werden endgültig gelöscht.\nKlick für die genaue Übersicht.",
     {L"Temporary files of programs. They are deleted permanently.\nClick for details.", L"Fichiers temporaires des programmes. Supprimés définitivement.\nClic pour l'aperçu détaillé.", L"Archivos temporales de programas. Se eliminan definitivamente.\nClic para ver el detalle.", L"Pliki tymczasowe programów. Usuwane na stałe.\nKliknij, aby zobaczyć szczegóły.", L"Arquivos temporários de programas. São excluídos definitivamente.\nClique para ver os detalhes.", L"プログラムの一時ファイルです。完全に削除されます。\nクリックで詳細を表示。", L"程式的暫存檔案。會永久刪除。\n點一下可查看詳細資訊。"}},
    {L"Vorschaubild-Cache und Absturzberichte von Windows.\nKlick für die genaue Übersicht.",
     {L"Thumbnail cache and crash reports from Windows.\nClick for details.", L"Cache des miniatures et rapports de plantage de Windows.\nClic pour l'aperçu détaillé.", L"Caché de miniaturas e informes de fallos de Windows.\nClic para ver el detalle.", L"Pamięć miniatur i raporty awarii Windows.\nKliknij, aby zobaczyć szczegóły.", L"Cache de miniaturas e relatórios de falhas do Windows.\nClique para ver os detalhes.", L"Windows のサムネイルキャッシュとクラッシュレポートです。\nクリックで詳細を表示。", L"Windows 的縮圖快取與當機報告。\n點一下可查看詳細資訊。"}},
    {L"Zwischenspeicher (Cache) deiner Browser. Keine Passwörter oder Cookies.\nKlick für die genaue Übersicht.",
     {L"Cache of your browsers. No passwords or cookies.\nClick for details.", L"Cache de vos navigateurs. Ni mots de passe ni cookies.\nClic pour l'aperçu détaillé.", L"Caché de tus navegadores. Sin contraseñas ni cookies.\nClic para ver el detalle.", L"Pamięć podręczna Twoich przeglądarek. Bez haseł i ciasteczek.\nKliknij, aby zobaczyć szczegóły.", L"Cache dos seus navegadores. Sem senhas nem cookies.\nClique para ver os detalhes.", L"ブラウザーのキャッシュです。パスワードや Cookie は含みません。\nクリックで詳細を表示。", L"你的瀏覽器快取。不含密碼或 Cookie。\n點一下可查看詳細資訊。"}},
    {L"Belegung und Zustand deiner Laufwerke. Nur Anzeige.\nKlick für die genaue Übersicht.",
     {L"Usage and health of your drives. Display only.\nClick for details.", L"Occupation et état de vos lecteurs. Affichage seul.\nClic pour l'aperçu détaillé.", L"Ocupación y estado de tus unidades. Solo visualización.\nClic para ver el detalle.", L"Zajętość i stan Twoich dysków. Tylko podgląd.\nKliknij, aby zobaczyć szczegóły.", L"Ocupação e estado das suas unidades. Somente exibição.\nClique para ver os detalhes.", L"ドライブの使用状況と状態です。表示のみ。\nクリックで詳細を表示。", L"磁碟機的使用量與狀態。僅供顯示。\n點一下可查看詳細資訊。"}},
    {L"Programme, die mit Windows starten. Hier ein- und ausschaltbar.\nKlick für die genaue Übersicht.",
     {L"Programs that start with Windows. Can be switched on and off here.\nClick for details.", L"Programmes qui démarrent avec Windows. Activables et désactivables ici.\nClic pour l'aperçu détaillé.", L"Programas que se inician con Windows. Aquí se pueden activar y desactivar.\nClic para ver el detalle.", L"Programy uruchamiane z Windows. Tutaj można je włączać i wyłączać.\nKliknij, aby zobaczyć szczegóły.", L"Programas que iniciam com o Windows. Podem ser ativados e desativados aqui.\nClique para ver os detalhes.", L"Windows と一緒に起動するプログラムです。ここでオン・オフできます。\nクリックで詳細を表示。", L"隨 Windows 啟動的程式。可在此開啟或關閉。\n點一下可查看詳細資訊。"}},
    {L"GitHub öffnen",
     {L"Open GitHub", L"Ouvrir GitHub", L"Abrir GitHub", L"Otwórz GitHub", L"Abrir GitHub", L"GitHub を開く", L"開啟 GitHub"}},
    {L"Schließen",
     {L"Close", L"Fermer", L"Cerrar", L"Zamknij", L"Fechar", L"閉じる", L"關閉"}},
    {L"Idee & Entwicklung: Duncan",
     {L"Idea & development: Duncan", L"Idée & développement : Duncan", L"Idea y desarrollo: Duncan", L"Pomysł i rozwój: Duncan", L"Ideia e desenvolvimento: Duncan", L"アイデアと開発：Duncan", L"構想與開發：Duncan"}},
    {L"Entwickelt zusammen mit Claude (Anthropic)",
     {L"Developed together with Claude (Anthropic)", L"Développé avec Claude (Anthropic)", L"Desarrollado junto con Claude (Anthropic)", L"Rozwijane razem z Claude (Anthropic)", L"Desenvolvido junto com o Claude (Anthropic)", L"Claude (Anthropic) と共同開発", L"與 Claude (Anthropic) 共同開發"}},
    {L"Teile des Designs: Oggy",
     {L"Parts of the design: Oggy", L"Une partie du design : Oggy", L"Parte del diseño: Oggy", L"Części designu: Oggy", L"Partes do design: Oggy", L"デザインの一部：Oggy", L"部分設計：Oggy"}},
    {L"© 2026 Duncan. Alle Rechte vorbehalten.",
     {L"© 2026 Duncan. All rights reserved.", L"© 2026 Duncan. Tous droits réservés.", L"© 2026 Duncan. Todos los derechos reservados.", L"© 2026 Duncan. Wszelkie prawa zastrzeżone.", L"© 2026 Duncan. Todos os direitos reservados.", L"© 2026 Duncan. All rights reserved.", L"© 2026 Duncan. 保留所有權利。"}},
    {L"Über Duncan's Tidy",
     {L"About Duncan's Tidy", L"À propos de Duncan's Tidy", L"Acerca de Duncan's Tidy", L"O programie Duncan's Tidy", L"Sobre o Duncan's Tidy", L"Duncan's Tidy について", L"關於 Duncan's Tidy"}},
    {L"Das ist neu",
     {L"What's new", L"Nouveautés", L"Novedades", L"Co nowego", L"Novidades", L"新機能", L"新功能"}},
    {L"Duncan's Tidy läuft weiter",
     {L"Duncan's Tidy keeps running", L"Duncan's Tidy continue de fonctionner", L"Duncan's Tidy sigue en ejecución", L"Duncan's Tidy nadal działa", L"O Duncan's Tidy continua em execução", L"Duncan's Tidy は動作を続けます", L"Duncan's Tidy 持續執行中"}},
    {L"Das Programm läuft im Hintergrund. Ein Klick auf das Symbol öffnet es wieder.",
     {L"The program is running in the background. Click the icon to open it again.", L"Le programme tourne en arrière-plan. Un clic sur l'icône le rouvre.", L"El programa se ejecuta en segundo plano. Un clic en el icono lo vuelve a abrir.", L"Program działa w tle. Kliknięcie ikony otwiera go ponownie.", L"O programa roda em segundo plano. Um clique no ícone o abre novamente.", L"プログラムはバックグラウンドで動作しています。アイコンをクリックすると再び開きます。", L"程式在背景執行。點一下圖示即可再次開啟。"}},
    {L"Duncan's Tidy öffnen",
     {L"Open Duncan's Tidy", L"Ouvrir Duncan's Tidy", L"Abrir Duncan's Tidy", L"Otwórz Duncan's Tidy", L"Abrir Duncan's Tidy", L"Duncan's Tidy を開く", L"開啟 Duncan's Tidy"}},
    {L"Beenden",
     {L"Quit", L"Quitter", L"Salir", L"Zakończ", L"Sair", L"終了", L"結束"}},
    {L"Exit",
     {L"Exit", L"Quitter", L"Salir", L"Wyjdź", L"Sair", L"終了", L"結束"}},
    {L"Alle / Keine",
     {L"All / None", L"Tout / Rien", L"Todo / Nada", L"Wszystko / Nic", L"Todos / Nenhum", L"すべて / なし", L"全選 / 全不選"}},
    {L"Schutzliste",
     {L"Protected list", L"Liste de protection", L"Lista de protección", L"Lista ochrony", L"Lista de proteção", L"保護リスト", L"保護清單"}},
    {L"Update verfügbar",
     {L"Update available", L"Mise à jour disponible", L"Actualización disponible", L"Dostępna aktualizacja", L"Atualização disponível", L"更新があります", L"有可用的更新"}},
    {L"Update auf v%s installieren",
     {L"Install update v%s", L"Installer la mise à jour v%s", L"Instalar actualización v%s", L"Zainstaluj aktualizację v%s", L"Instalar atualização v%s", L"v%s の更新をインストール", L"安裝更新 v%s"}},
    {L"Programm suchen …",
     {L"Search programs …", L"Rechercher un programme …", L"Buscar programa …", L"Szukaj programu …", L"Procurar programa …", L"プログラムを検索 …", L"搜尋程式 …"}},
    {L" Einträge gelöscht (",
     {L" entries deleted (", L" éléments supprimés (", L" elementos eliminados (", L" elementów usunięto (", L" itens excluídos (", L" 件を削除しました (", L" 項已刪除 ("}},
    {L" frei), ",
     {L" freed), ", L" libérés), ", L" liberados), ", L" zwolniono), ", L" liberados), ", L" 解放)、 ", L" 已釋放)， "}},
    {L" fehlgeschlagen.",
     {L" failed.", L" échec(s).", L" fallidos.", L" nie powiodło się.", L" com falha.", L" 件失敗。", L" 項失敗。"}},
    {L" in Benutzung/übersprungen.",
     {L" in use/skipped.", L" en cours d'utilisation/ignorés.", L" en uso/omitidos.", L" w użyciu/pominięto.", L" em uso/ignorados.", L" 件は使用中のためスキップ。", L" 項因使用中或已略過。"}},
    {L"\n\nÜbersprungen: ",
     {L"\n\nSkipped: ", L"\n\nIgnorés : ", L"\n\nOmitidos: ", L"\n\nPominięto: ", L"\n\nIgnorados: ", L"\n\nスキップ： ", L"\n\n已略過： "}},
    {L" Speicherplatz erfolgreich freigegeben",
     {L" of disk space freed successfully", L" d'espace disque libérés avec succès", L" de espacio en disco liberados correctamente", L" miejsca na dysku zwolniono pomyślnie", L" de espaço em disco liberados com sucesso", L" の空き容量を確保しました", L" 磁碟空間已成功釋放"}},
    {L" Einträge entfernt",
     {L" entries removed", L" éléments supprimés", L" elementos eliminados", L" elementów usunięto", L" itens removidos", L" 件を削除しました", L" 項已移除"}},
    {L" übersprungen (in Benutzung)",
     {L" skipped (in use)", L" ignorés (en cours d'utilisation)", L" omitidos (en uso)", L" pominięto (w użyciu)", L" ignorados (em uso)", L" 件をスキップ (使用中)", L" 項已略過 (使用中)"}},
    {L"  ·  Downloads liegen im Papierkorb",
     {L"  ·  Downloads are in the Recycle Bin", L"  ·  Les téléchargements sont dans la Corbeille", L"  ·  Las descargas están en la Papelera", L"  ·  Pobrane leżą w Koszu", L"  ·  Os downloads estão na Lixeira", L"  ·  ダウンロードはごみ箱にあります", L"  ·  「下載」的內容在資源回收筒中"}},
    {L"Update installiert",
     {L"Update installed", L"Mise à jour installée", L"Actualización instalada", L"Aktualizacja zainstalowana", L"Atualização instalada", L"更新をインストールしました", L"更新已安裝"}},
    {L"Verbesserungen und Fehlerbehebungen.",
     {L"Improvements and bug fixes.", L"Améliorations et corrections de bugs.", L"Mejoras y correcciones de errores.", L"Ulepszenia i poprawki błędów.", L"Melhorias e correções de erros.", L"改善とバグ修正。", L"改進與錯誤修正。"}},
    {L"Viel Spaß mit der neuen Version!",
     {L"Enjoy the new version!", L"Profitez de la nouvelle version !", L"¡Que disfrutes de la nueva versión!", L"Miłego korzystania z nowej wersji!", L"Aproveite a nova versão!", L"新しいバージョンをお楽しみください！", L"祝你使用新版本愉快！"}},
    {L"Los geht's",
     {L"Let's go", L"C'est parti", L"Vamos", L"Zaczynamy", L"Vamos lá", L"始める", L"開始吧"}},
    {L"Das Programm lädt das Update herunter, prüft es und startet danach automatisch neu.",
     {L"The program downloads the update, checks it and then restarts automatically.", L"Le programme télécharge la mise à jour, la vérifie puis redémarre automatiquement.", L"El programa descarga la actualización, la comprueba y se reinicia automáticamente.", L"Program pobiera aktualizację, sprawdza ją i uruchamia się ponownie automatycznie.", L"O programa baixa a atualização, verifica e reinicia automaticamente.", L"プログラムが更新をダウンロードして確認し、自動的に再起動します。", L"程式會下載更新、檢查後自動重新啟動。"}},
    {L"Jetzt aktualisieren",
     {L"Update now", L"Mettre à jour maintenant", L"Actualizar ahora", L"Aktualizuj teraz", L"Atualizar agora", L"今すぐ更新", L"立即更新"}},
    {L"Später",
     {L"Later", L"Plus tard", L"Más tarde", L"Później", L"Mais tarde", L"あとで", L"稍後"}},
    {L"Duncan's Tidy ist noch nicht installiert. Jetzt installieren?\n\n",
     {L"Duncan's Tidy is not installed yet. Install now?\n\n", L"Duncan's Tidy n'est pas encore installé. L'installer maintenant ?\n\n", L"Duncan's Tidy aún no está instalado. ¿Instalarlo ahora?\n\n", L"Duncan's Tidy nie jest jeszcze zainstalowany. Zainstalować teraz?\n\n", L"O Duncan's Tidy ainda não está instalado. Instalar agora?\n\n", L"Duncan's Tidy はまだインストールされていません。今すぐインストールしますか？\n\n", L"Duncan's Tidy 尚未安裝。要現在安裝嗎？\n\n"}},
    {L" • Räumt den Downloads-Ordner alle 30 Tage automatisch auf (mit Rückfrage)\n",
     {L" • Tidies the Downloads folder automatically every 30 days (with confirmation)\n", L" • Nettoie automatiquement le dossier Téléchargements tous les 30 jours (avec confirmation)\n", L" • Limpia la carpeta Descargas automáticamente cada 30 días (con confirmación)\n", L" • Porządkuje folder Pobrane automatycznie co 30 dni (z pytaniem)\n", L" • Limpa a pasta Downloads automaticamente a cada 30 dias (com confirmação)\n", L" • ダウンロードフォルダーを 30 日ごとに自動で整理します (確認あり)\n", L" • 每 30 天自動整理「下載」資料夾 (會先詢問)\n"}},
    {L" • Legt eine Verknüpfung im Startmenü an\n",
     {L" • Creates a Start menu shortcut\n", L" • Crée un raccourci dans le menu Démarrer\n", L" • Crea un acceso directo en el menú Inicio\n", L" • Tworzy skrót w menu Start\n", L" • Cria um atalho no menu Iniciar\n", L" • スタートメニューにショートカットを作成します\n", L" • 在開始功能表建立捷徑\n"}},
    {L" • Braucht keine Administrator-Rechte\n\n",
     {L" • Needs no administrator rights\n\n", L" • Ne nécessite aucun droit d'administrateur\n\n", L" • No necesita derechos de administrador\n\n", L" • Nie wymaga uprawnień administratora\n\n", L" • Não precisa de direitos de administrador\n\n", L" • 管理者権限は不要です\n\n", L" • 不需要系統管理員權限\n\n"}},
    {L"„Nein“ startet das Programm ohne Installation (und fragt nicht wieder).",
     {L"\"No\" starts the program without installing (and does not ask again).", L"« Non » lance le programme sans installation (et ne redemande pas).", L"«No» inicia el programa sin instalarlo (y no vuelve a preguntar).", L"„Nie” uruchamia program bez instalacji (i nie pyta ponownie).", L"«Não» inicia o programa sem instalar (e não pergunta de novo).", L"「いいえ」を選ぶと、インストールせずに起動します (今後は確認しません)。", L"選「否」會不安裝直接啟動程式 (之後不再詢問)。"}},
    {L"Soll der Downloads-Ordner alle 30 Tage automatisch aufgeräumt werden?\n\n",
     {L"Should the Downloads folder be tidied automatically every 30 days?\n\n", L"Faut-il nettoyer automatiquement le dossier Téléchargements tous les 30 jours ?\n\n", L"¿Limpiar automáticamente la carpeta Descargas cada 30 días?\n\n", L"Czy folder Pobrane ma być porządkowany automatycznie co 30 dni?\n\n", L"Limpar a pasta Downloads automaticamente a cada 30 dias?\n\n", L"ダウンロードフォルダーを 30 日ごとに自動で整理しますか？\n\n", L"要每 30 天自動整理「下載」資料夾嗎？\n\n"}},
    {L" • Vorher kommt immer eine Rückfrage\n",
     {L" • You are always asked first\n", L" • Une confirmation est toujours demandée avant\n", L" • Antes siempre se pide confirmación\n", L" • Wcześniej zawsze pojawi się pytanie\n", L" • Antes sempre há uma confirmação\n", L" • 実行前に必ず確認します\n", L" • 執行前一定會先詢問\n"}},
    {L" • Alles landet erst im Papierkorb\n",
     {L" • Everything goes to the Recycle Bin first\n", L" • Tout va d'abord dans la Corbeille\n", L" • Todo va primero a la Papelera\n", L" • Wszystko trafia najpierw do Kosza\n", L" • Tudo vai primeiro para a Lixeira\n", L" • すべて、まずごみ箱に入ります\n", L" • 所有內容會先進入資源回收筒\n"}},
    {L" • Du kannst das jederzeit in den Einstellungen ein- oder ausschalten",
     {L" • You can switch this on or off at any time in Settings", L" • Vous pouvez l'activer ou le désactiver à tout moment dans les paramètres", L" • Puedes activarlo o desactivarlo en cualquier momento en Ajustes", L" • Możesz to w każdej chwili włączyć lub wyłączyć w ustawieniach", L" • Você pode ativar ou desativar isso a qualquer momento nas configurações", L" • 設定でいつでもオン・オフできます", L" • 你隨時可以在設定中開啟或關閉"}},
    {L"Eine ältere Version von Duncan's Tidy ist installiert. Auf diese Version aktualisieren?",
     {L"An older version of Duncan's Tidy is installed. Update to this version?", L"Une version plus ancienne de Duncan's Tidy est installée. Passer à cette version ?", L"Hay instalada una versión más antigua de Duncan's Tidy. ¿Actualizar a esta versión?", L"Zainstalowana jest starsza wersja Duncan's Tidy. Zaktualizować do tej wersji?", L"Há uma versão mais antiga do Duncan's Tidy instalada. Atualizar para esta versão?", L"古いバージョンの Duncan's Tidy がインストールされています。このバージョンに更新しますか？", L"已安裝較舊版本的 Duncan's Tidy。要更新為這個版本嗎？"}},
    {L"Die Installation ging nicht. Ist Duncan's Tidy schon geöffnet? Bitte erst schließen und noch einmal versuchen.",
     {L"Installation failed. Is Duncan's Tidy already open? Please close it and try again.", L"L'installation a échoué. Duncan's Tidy est-il déjà ouvert ? Veuillez le fermer et réessayer.", L"No se pudo instalar. ¿Duncan's Tidy ya está abierto? Ciérralo e inténtalo de nuevo.", L"Instalacja się nie powiodła. Czy Duncan's Tidy jest już otwarty? Zamknij go i spróbuj ponownie.", L"A instalação falhou. O Duncan's Tidy já está aberto? Feche-o e tente novamente.", L"インストールできませんでした。Duncan's Tidy がすでに開いていませんか？閉じてからもう一度お試しください。", L"安裝失敗。Duncan's Tidy 是否已經開啟？請先關閉後再試一次。"}},
    {L"Das Programm wurde kopiert, aber die automatische Bereinigung beim Anmelden konnte nicht eingerichtet werden.",
     {L"The program was copied, but automatic cleaning at login could not be set up.", L"Le programme a été copié, mais le nettoyage automatique à l'ouverture de session n'a pas pu être configuré.", L"El programa se copió, pero no se pudo configurar la limpieza automática al iniciar sesión.", L"Program został skopiowany, ale nie udało się skonfigurować automatycznego czyszczenia przy logowaniu.", L"O programa foi copiado, mas a limpeza automática no login não pôde ser configurada.", L"プログラムはコピーされましたが、サインイン時の自動クリーンを設定できませんでした。", L"程式已複製，但無法設定登入時的自動清理。"}},
    {L"Downloads",
     {L"Downloads", L"Téléchargements", L"Descargas", L"Pobrane", L"Downloads", L"ダウンロード", L"下載"}},
    {L"Temp-Dateien",
     {L"Temp files", L"Fichiers temp.", L"Temporales", L"Tymczasowe", L"Temporários", L"一時ファイル", L"暫存檔案"}},
    {L"Systemdateien",
     {L"System files", L"Fichiers système", L"Sistema", L"Systemowe", L"Sistema", L"システム", L"系統檔案"}},
    {L"Browser-Cache",
     {L"Browser cache", L"Cache des navigateurs", L"Caché del navegador", L"Cache przeglądarek", L"Cache do navegador", L"ブラウザーのキャッシュ", L"瀏覽器快取"}},
    {L"landet im Papierkorb",
     {L"goes to the Recycle Bin", L"va dans la Corbeille", L"va a la Papelera", L"trafia do Kosza", L"vai para a Lixeira", L"ごみ箱に移動", L"會移到資源回收筒"}},
    {L"wird endgültig gelöscht",
     {L"permanently deleted", L"supprimé définitivement", L"se elimina definitivamente", L"usuwane na stałe", L"excluído definitivamente", L"完全に削除", L"永久刪除"}},
    {L"Vorschau-Cache, Berichte",
     {L"Thumbnail cache, reports", L"Cache des miniatures, rapports", L"Caché de miniaturas, informes", L"Pamięć miniatur, raporty", L"Cache de miniaturas, relatórios", L"サムネイルキャッシュ、レポート", L"縮圖快取、報告"}},
    {L"Zwischenspeicher",
     {L"Cache", L"Cache", L"Caché", L"Pamięć podręczna", L"Cache", L"キャッシュ", L"快取"}},
    {L"Übersicht",
     {L"Overview", L"Aperçu", L"Resumen", L"Przegląd", L"Visão geral", L"概要", L"總覽"}},
    {L"Browser",
     {L"Browser", L"Navigateurs", L"Navegadores", L"Przeglądarki", L"Navegadores", L"ブラウザー", L"瀏覽器"}},
    {L"Einstellungen",
     {L"Settings", L"Paramètres", L"Ajustes", L"Ustawienia", L"Configurações", L"設定", L"設定"}},
    {L"Programm-Ordner",
     {L"Program folders", L"Dossiers de programmes", L"Carpetas de programas", L"Foldery programów", L"Pastas de programas", L"プログラムのフォルダー", L"程式資料夾"}},
    {L"Ordner mit Zufallsnamen",
     {L"Folders with random names", L"Dossiers à noms aléatoires", L"Carpetas con nombres aleatorios", L"Foldery o losowych nazwach", L"Pastas com nomes aleatórios", L"ランダム名のフォルダー", L"隨機名稱的資料夾"}},
    {L"Zwischendateien (.tmp)",
     {L"Temporary files (.tmp)", L"Fichiers intermédiaires (.tmp)", L"Archivos intermedios (.tmp)", L"Pliki pośrednie (.tmp)", L"Arquivos intermediários (.tmp)", L"中間ファイル (.tmp)", L"中繼檔案 (.tmp)"}},
    {L"Protokolle und Berichte",
     {L"Logs and reports", L"Journaux et rapports", L"Registros e informes", L"Dzienniki i raporty", L"Logs e relatórios", L"ログとレポート", L"記錄與報告"}},
    {L"Installationsdateien",
     {L"Installer files", L"Fichiers d'installation", L"Archivos de instalación", L"Pliki instalacyjne", L"Arquivos de instalação", L"インストールファイル", L"安裝檔案"}},
    {L"Sonstige Dateien",
     {L"Other files", L"Autres fichiers", L"Otros archivos", L"Inne pliki", L"Outros arquivos", L"その他のファイル", L"其他檔案"}},
    {L"Zwischenordner einzelner Programme mit lesbaren Namen",
     {L"Intermediate folders of individual programs with readable names", L"Dossiers intermédiaires de programmes, à noms lisibles", L"Carpetas intermedias de programas con nombres legibles", L"Foldery pośrednie programów o czytelnych nazwach", L"Pastas intermediárias de programas com nomes legíveis", L"プログラムごとの、読みやすい名前の中間フォルダー", L"各程式名稱易讀的中繼資料夾"}},
    {L"Vorschaubilder",
     {L"Thumbnails", L"Miniatures", L"Miniaturas", L"Miniatury", L"Miniaturas", L"サムネイル", L"縮圖"}},
    {L"Symbol-Cache",
     {L"Icon cache", L"Cache des icônes", L"Caché de iconos", L"Pamięć ikon", L"Cache de ícones", L"アイコンキャッシュ", L"圖示快取"}},
    {L"Absturzabbilder",
     {L"Crash dumps", L"Vidages sur incident", L"Volcados de bloqueo", L"Zrzuty awarii", L"Despejos de falha", L"クラッシュダンプ", L"當機傾印檔"}},
    {L"Windows-Fehlerberichte",
     {L"Windows error reports", L"Rapports d'erreurs Windows", L"Informes de errores de Windows", L"Raporty błędów Windows", L"Relatórios de erros do Windows", L"Windows エラーレポート", L"Windows 錯誤報告"}},
    {L"Sonstiges",
     {L"Other", L"Divers", L"Varios", L"Inne", L"Diversos", L"その他", L"其他"}},
    {L"Zwischenspeicher für Vorschaubilder im Explorer, wird neu aufgebaut",
     {L"Cache for thumbnails in Explorer, rebuilt automatically", L"Cache des miniatures de l'Explorateur, reconstruit automatiquement", L"Caché de miniaturas del Explorador, se reconstruye sola", L"Pamięć miniatur w Eksploratorze, odbudowywana automatycznie", L"Cache de miniaturas do Explorer, é reconstruído automaticamente", L"エクスプローラーのサムネイル用キャッシュ。自動的に再作成されます", L"檔案總管的縮圖快取，會自動重新建立"}},
    {L"Zwischenspeicher für Symbole im Explorer, wird neu aufgebaut",
     {L"Cache for icons in Explorer, rebuilt automatically", L"Cache des icônes de l'Explorateur, reconstruit automatiquement", L"Caché de iconos del Explorador, se reconstruye sola", L"Pamięć ikon w Eksploratorze, odbudowywana automatycznie", L"Cache de ícones do Explorer, é reconstruído automaticamente", L"エクスプローラーのアイコン用キャッシュ。自動的に再作成されます", L"檔案總管的圖示快取，會自動重新建立"}},
    {L"Speicherabbilder abgestürzter Programme",
     {L"Memory dumps of crashed programs", L"Vidages mémoire des programmes plantés", L"Volcados de memoria de programas bloqueados", L"Zrzuty pamięci programów, które uległy awarii", L"Despejos de memória de programas que falharam", L"クラッシュしたプログラムのメモリダンプ", L"當機程式的記憶體傾印檔"}},
    {L"Von Windows gesammelte Fehlerberichte",
     {L"Error reports collected by Windows", L"Rapports d'erreurs collectés par Windows", L"Informes de errores recopilados por Windows", L"Raporty błędów zbierane przez Windows", L"Relatórios de erros coletados pelo Windows", L"Windows が収集したエラーレポート", L"Windows 收集的錯誤報告"}},
    {L"Weitere Dateien",
     {L"More files", L"Autres fichiers", L"Más archivos", L"Więcej plików", L"Mais arquivos", L"その他のファイル", L"更多檔案"}},
    {L"Mit Windows starten",
     {L"Start with Windows", L"Démarrer avec Windows", L"Iniciar con Windows", L"Uruchamiaj z Windows", L"Iniciar com o Windows", L"Windows と一緒に起動", L"隨 Windows 啟動"}},
    {L"Im Hintergrund weiterlaufen",
     {L"Keep running in the background", L"Continuer en arrière-plan", L"Seguir en segundo plano", L"Działaj dalej w tle", L"Continuar em segundo plano", L"バックグラウンドで動作を続ける", L"在背景持續執行"}},
    {L"Downloads automatisch aufräumen",
     {L"Tidy Downloads automatically", L"Nettoyer automatiquement les Téléchargements", L"Limpiar Descargas automáticamente", L"Automatycznie porządkuj Pobrane", L"Limpar Downloads automaticamente", L"ダウンロードを自動で整理", L"自動整理「下載」"}},
    {L"Helles Design",
     {L"Light theme", L"Thème clair", L"Tema claro", L"Jasny motyw", L"Tema claro", L"ライトテーマ", L"淺色主題"}},
    {L"Besen-Animation",
     {L"Broom animation", L"Animation du balai", L"Animación de la escoba", L"Animacja miotły", L"Animação da vassoura", L"ほうきのアニメーション", L"掃帚動畫"}},
    {L"Sprache / Language",
     {L"Sprache / Language", L"Sprache / Language", L"Sprache / Language", L"Sprache / Language", L"Sprache / Language", L"Sprache / Language", L"Sprache / Language"}},
    {L"Reste von Installationen und Updates (Namen wie {3F2A19C4-…})",
     {L"Leftovers from installations and updates (names like {3F2A19C4-…})", L"Restes d'installations et de mises à jour (noms comme {3F2A19C4-…})", L"Restos de instalaciones y actualizaciones (nombres como {3F2A19C4-…})", L"Pozostałości instalacji i aktualizacji (nazwy jak {3F2A19C4-…})", L"Restos de instalações e atualizações (nomes como {3F2A19C4-…})", L"インストールや更新の残り (名前は {3F2A19C4-…} のような形式)", L"安裝與更新的殘留項目 (名稱類似 {3F2A19C4-…})"}},
    {L"Kurzlebige Arbeitsdateien von Programmen",
     {L"Short-lived working files of programs", L"Fichiers de travail éphémères des programmes", L"Archivos de trabajo efímeros de programas", L"Krótkotrwałe pliki robocze programów", L"Arquivos de trabalho de curta duração de programas", L"プログラムの短命な作業ファイル", L"程式的短暫工作檔案"}},
    {L"Logdateien und Textprotokolle",
     {L"Log files and text logs", L"Fichiers journaux et protocoles texte", L"Archivos de registro y protocolos de texto", L"Pliki dziennika i protokoły tekstowe", L"Arquivos de log e protocolos de texto", L"ログファイルとテキストの記録", L"記錄檔與文字記錄"}},
    {L"Entpackte Installer und Update-Pakete",
     {L"Unpacked installers and update packages", L"Installateurs décompressés et paquets de mise à jour", L"Instaladores descomprimidos y paquetes de actualización", L"Rozpakowane instalatory i pakiety aktualizacji", L"Instaladores descompactados e pacotes de atualização", L"展開されたインストーラーと更新パッケージ", L"已解壓縮的安裝程式與更新套件"}},
    {L"Alles andere, was sich nicht eindeutig zuordnen lässt",
     {L"Everything else that cannot be clearly assigned", L"Tout le reste, qui ne peut pas être clairement classé", L"Todo lo demás que no se puede clasificar con claridad", L"Wszystko inne, czego nie da się jednoznacznie przypisać", L"Todo o resto que não dá para classificar com clareza", L"はっきり分類できないその他すべて", L"其他無法明確歸類的所有項目"}},
    {L"Startet beim Anmelden unsichtbar im System-Tray (neben der Uhr).",
     {L"Starts invisibly in the system tray (next to the clock) when you sign in.", L"Démarre invisiblement dans la zone de notification (près de l'horloge) à l'ouverture de session.", L"Se inicia sin ventana en la bandeja del sistema (junto al reloj) al iniciar sesión.", L"Uruchamia się niewidocznie w zasobniku systemowym (obok zegara) przy logowaniu.", L"Inicia invisível na bandeja do sistema (ao lado do relógio) ao fazer login.", L"サインイン時にタスクトレイ (時計の横) に表示せず起動します。", L"登入時會在系統匣 (時鐘旁) 隱藏啟動。"}},
    {L"X minimiert in den Tray. Beenden über „Exit“ oder Rechtsklick auf das Symbol.",
     {L"X minimizes to the tray. Quit via \"Exit\" or right-click the tray icon.", L"La croix réduit dans la zone de notification. Quitter via « Quitter » ou clic droit sur l'icône.", L"La X minimiza a la bandeja. Salir con «Salir» o clic derecho en el icono.", L"X minimalizuje do zasobnika. Zakończ przez „Wyjdź” lub prawy przycisk na ikonie.", L"O X minimiza para a bandeja. Sair por «Sair» ou clique direito no ícone.", L"× でトレイに最小化します。終了は「終了」またはアイコンの右クリックから。", L"按 X 會縮小到系統匣。請用「結束」或在圖示上按右鍵結束。"}},
    {L"Räumt Downloads im gewählten Intervall auf (erst in den Papierkorb, kleine Dateien ausnehmbar). Optional auch Temp-Dateien, Vorschau-/Symbol-Cache sowie Absturz- und Fehlerberichte – die werden endgültig gelöscht; benutzte Dateien und Schutzliste bleiben.",
     {L"Tidies Downloads at the chosen interval (Recycle Bin first, small files can be excluded). Optionally also temp files, thumbnail/icon cache and crash and error reports – those are deleted permanently; files in use and the protected list are kept.", L"Nettoie les Téléchargements à l'intervalle choisi (d'abord dans la Corbeille, petits fichiers excluables). En option aussi : fichiers temporaires, cache des miniatures/icônes, rapports de plantage et d'erreurs – supprimés définitivement ; fichiers utilisés et liste de protection conservés.", L"Limpia Descargas en el intervalo elegido (primero a la Papelera, se pueden excluir archivos pequeños). Opcionalmente también archivos temporales, caché de miniaturas/iconos e informes de fallos y errores – se eliminan definitivamente; los archivos en uso y la lista de protección se conservan.", L"Porządkuje Pobrane w wybranym odstępie (najpierw do Kosza, małe pliki można wykluczyć). Opcjonalnie także pliki tymczasowe, pamięć miniatur/ikon oraz raporty awarii i błędów – usuwane na stałe; używane pliki i lista ochrony zostają.", L"Limpa Downloads no intervalo escolhido (primeiro para a Lixeira, arquivos pequenos podem ser excluídos). Opcionalmente também arquivos temporários, cache de miniaturas/ícones e relatórios de falhas e erros – excluídos definitivamente; arquivos em uso e a lista de proteção são mantidos.", L"選んだ間隔でダウンロードを整理します (まずごみ箱へ。小さいファイルは除外できます)。オプションで一時ファイル、サムネイル・アイコンのキャッシュ、クラッシュ・エラーレポートも整理します – これらは完全に削除され、使用中のファイルと保護リストは保持されます。", L"依選定的間隔整理「下載」(先進入資源回收筒，小檔案可排除)。也可選擇一併整理暫存檔案、縮圖與圖示快取，以及當機與錯誤報告 – 這些會永久刪除；使用中的檔案與保護清單會保留。"}},
    {L"Aus = dunkles Design (wie bisher), an = helles Design. Der Wechsel gilt sofort.",
     {L"Off = dark theme (as before), on = light theme. The change applies immediately.", L"Désactivé = thème sombre (comme avant), activé = thème clair. Le changement est immédiat.", L"Desactivado = tema oscuro (como hasta ahora), activado = tema claro. El cambio es inmediato.", L"Wył. = ciemny motyw (jak dotąd), wł. = jasny motyw. Zmiana działa od razu.", L"Desativado = tema escuro (como antes), ativado = tema claro. A mudança vale na hora.", L"オフ = ダークテーマ (従来どおり)、オン = ライトテーマ。切り替えはすぐに反映されます。", L"關 = 深色主題 (和以前一樣)，開 = 淺色主題。切換會立即生效。"}},
    {L"Der Besen in der Kopfzeile fegt beim Start und beim Aufräumen.",
     {L"The broom in the header sweeps at startup and while cleaning.", L"Le balai dans l'en-tête balaie au démarrage et pendant le nettoyage.", L"La escoba de la cabecera barre al iniciar y al limpiar.", L"Miotła w nagłówku zamiata przy starcie i podczas sprzątania.", L"A vassoura no cabeçalho varre ao iniciar e ao limpar.", L"ヘッダーのほうきが、起動時と整理中に掃きます。", L"標題列的掃帚會在啟動和整理時掃動。"}},
    {L"Sprache der Oberfläche. Ein Klick öffnet die Auswahl.",
     {L"Interface language. A click opens the list.", L"Langue de l'interface. Un clic ouvre la liste.", L"Idioma de la interfaz. Un clic abre la lista.", L"Język interfejsu. Kliknięcie otwiera listę.", L"Idioma da interface. Um clique abre a lista.", L"表示言語です。クリックすると一覧が開きます。", L"介面語言。點一下會開啟選單。"}},
    {L"von %d aktiv",
     {L"of %d active", L"sur %d actifs", L"de %d activos", L"z %d aktywnych", L"de %d ativos", L"有効 · 全 %d 個", L"已啟用 · 共 %d 個"}},
    {L"Dokumente",
     {L"Documents", L"Documents", L"Documentos", L"Dokumenty", L"Documentos", L"ドキュメント", L"文件"}},
    {L"Bilder",
     {L"Images", L"Images", L"Imágenes", L"Obrazy", L"Imagens", L"画像", L"圖片"}},
    {L"Videos",
     {L"Videos", L"Vidéos", L"Vídeos", L"Wideo", L"Vídeos", L"動画", L"影片"}},
    {L"Musik",
     {L"Music", L"Musique", L"Música", L"Muzyka", L"Música", L"音楽", L"音樂"}},
    {L"Archive",
     {L"Archives", L"Archives", L"Archivos comprimidos", L"Archiwa", L"Arquivos compactados", L"アーカイブ", L"壓縮檔"}},
    {L"Programme und Installer",
     {L"Programs and installers", L"Programmes et installateurs", L"Programas e instaladores", L"Programy i instalatory", L"Programas e instaladores", L"プログラムとインストーラー", L"程式與安裝程式"}},
    {L"Ordner",
     {L"Folders", L"Dossiers", L"Carpetas", L"Foldery", L"Pastas", L"フォルダー", L"資料夾"}},
    {L"PDF, Word, Excel, PowerPoint, Text …",
     {L"PDF, Word, Excel, PowerPoint, text …", L"PDF, Word, Excel, PowerPoint, texte …", L"PDF, Word, Excel, PowerPoint, texto …", L"PDF, Word, Excel, PowerPoint, tekst …", L"PDF, Word, Excel, PowerPoint, texto …", L"PDF、Word、Excel、PowerPoint、テキスト …", L"PDF、Word、Excel、PowerPoint、文字檔 …"}},
    {L"JPG, PNG, GIF, WebP …",
     {L"JPG, PNG, GIF, WebP …", L"JPG, PNG, GIF, WebP …", L"JPG, PNG, GIF, WebP …", L"JPG, PNG, GIF, WebP …", L"JPG, PNG, GIF, WebP …", L"JPG, PNG, GIF, WebP …", L"JPG, PNG, GIF, WebP …"}},
    {L"MP4, MKV, AVI, MOV …",
     {L"MP4, MKV, AVI, MOV …", L"MP4, MKV, AVI, MOV …", L"MP4, MKV, AVI, MOV …", L"MP4, MKV, AVI, MOV …", L"MP4, MKV, AVI, MOV …", L"MP4, MKV, AVI, MOV …", L"MP4, MKV, AVI, MOV …"}},
    {L"MP3, WAV, FLAC, M4A …",
     {L"MP3, WAV, FLAC, M4A …", L"MP3, WAV, FLAC, M4A …", L"MP3, WAV, FLAC, M4A …", L"MP3, WAV, FLAC, M4A …", L"MP3, WAV, FLAC, M4A …", L"MP3, WAV, FLAC, M4A …", L"MP3, WAV, FLAC, M4A …"}},
    {L"ZIP, RAR, 7z, ISO …",
     {L"ZIP, RAR, 7z, ISO …", L"ZIP, RAR, 7z, ISO …", L"ZIP, RAR, 7z, ISO …", L"ZIP, RAR, 7z, ISO …", L"ZIP, RAR, 7z, ISO …", L"ZIP, RAR, 7z, ISO …", L"ZIP, RAR, 7z, ISO …"}},
    {L"Installer, EXE, MSI, Skripte …",
     {L"Installers, EXE, MSI, scripts …", L"Installateurs, EXE, MSI, scripts …", L"Instaladores, EXE, MSI, scripts …", L"Instalatory, EXE, MSI, skrypty …", L"Instaladores, EXE, MSI, scripts …", L"インストーラー、EXE、MSI、スクリプト …", L"安裝程式、EXE、MSI、指令碼 …"}},
    {L"Ganze Ordner im Downloads-Ordner",
     {L"Whole folders in the Downloads folder", L"Dossiers entiers dans Téléchargements", L"Carpetas completas en Descargas", L"Całe foldery w folderze Pobrane", L"Pastas inteiras na pasta Downloads", L"ダウンロードフォルダー内のフォルダー全体", L"「下載」資料夾中的整個資料夾"}},
    {L"Programme",
     {L"Programs", L"Programmes", L"Programas", L"Programy", L"Programas", L"プログラム", L"程式"}},
    {L"Installierte Programme",
     {L"Installed programs", L"Programmes installés", L"Programas instalados", L"Zainstalowane programy", L"Programas instalados", L"インストール済みプログラム", L"已安裝的程式"}},
    {L" Programme installiert  ·  ",
     {L" programs installed  ·  ", L" programmes installés  ·  ", L" programas instalados  ·  ", L" zainstalowanych programów  ·  ", L" programas instalados  ·  ", L" 個のプログラムをインストール済み  ·  ", L" 個已安裝的程式  ·  "}},
    {L" belegt",
     {L" used", L" occupés", L" ocupados", L" zajęte", L" ocupados", L" 使用中", L" 已使用"}},
    {L"Sortieren: Größe",
     {L"Sort: size", L"Tri : taille", L"Orden: tamaño", L"Sortuj: rozmiar", L"Ordem: tamanho", L"並び替え：サイズ", L"排序：大小"}},
    {L"Sortieren: Name",
     {L"Sort: name", L"Tri : nom", L"Orden: nombre", L"Sortuj: nazwa", L"Ordem: nome", L"並び替え：名前", L"排序：名稱"}},
    {L"Deinstallieren",
     {L"Uninstall", L"Désinstaller", L"Desinstalar", L"Odinstaluj", L"Desinstalar", L"アンインストール", L"解除安裝"}},
    {L"Analysiere Programme …",
     {L"Analyzing programs …", L"Analyse des programmes …", L"Analizando programas …", L"Analizowanie programów …", L"Analisando programas …", L"プログラムを分析中 …", L"正在分析程式 …"}},
    {L"Keine Programme gefunden",
     {L"No programs found", L"Aucun programme trouvé", L"No se encontraron programas", L"Nie znaleziono programów", L"Nenhum programa encontrado", L"プログラムが見つかりません", L"找不到程式"}},
    {L"„%s“ deinstallieren?\n\nDas Deinstallationsprogramm des Programms wird gestartet. Dort kannst du noch abbrechen.",
     {L"Uninstall “%s”?\n\nThe program’s own uninstaller will be started. You can still cancel there.", L"Désinstaller « %s » ?\n\nLe programme de désinstallation du programme va démarrer. Vous pourrez encore annuler à ce moment-là.", L"¿Desinstalar «%s»?\n\nSe iniciará el desinstalador del propio programa. Allí aún puedes cancelar.", L"Odinstalować „%s”?\n\nUruchomi się własny deinstalator programu. Tam nadal możesz anulować.", L"Desinstalar «%s»?\n\nO desinstalador do próprio programa será iniciado. Lá você ainda pode cancelar.", L"「%s」をアンインストールしますか？\n\nプログラム自身のアンインストーラーが起動します。そこでまだキャンセルできます。", L"要解除安裝「%s」嗎？\n\n將啟動該程式自己的解除安裝程式。你仍可在那裡取消。"}},
    {L"Das Deinstallationsprogramm konnte nicht gestartet werden.",
     {L"The uninstaller could not be started.", L"Impossible de lancer le programme de désinstallation.", L"No se pudo iniciar el desinstalador.", L"Nie udało się uruchomić deinstalatora.", L"Não foi possível iniciar o desinstalador.", L"アンインストーラーを起動できませんでした。", L"無法啟動解除安裝程式。"}},
    {L"Installierte Programme. Deinstallieren startet den Uninstaller des Programms.\nKlick für die genaue Übersicht.",
     {L"Installed programs. Uninstall starts the program’s own uninstaller.\nClick for details.", L"Programmes installés. « Désinstaller » lance le désinstallateur du programme.\nClic pour l’aperçu détaillé.", L"Programas instalados. Desinstalar inicia el desinstalador del programa.\nClic para ver el detalle.", L"Zainstalowane programy. Odinstaluj uruchamia deinstalator programu.\nKliknij, aby zobaczyć szczegóły.", L"Programas instalados. Desinstalar inicia o desinstalador do programa.\nClique para ver os detalhes.", L"インストール済みのプログラムです。「アンインストール」でプログラム自身のアンインストーラーを起動します。\nクリックで詳細を表示。", L"已安裝的程式。「解除安裝」會啟動該程式自己的解除安裝程式。\n點一下可查看詳細資訊。"}},
    // Mehr App-Caches (1.5.0)
    {L"Zwischenspeicher von Discord, Slack, Steam, VS Code, Teams, Spotify und weiteren Programmen – Anmeldung und Einstellungen bleiben, die Programme bauen den Cache neu auf",
     {L"Caches of Discord, Slack, Steam, VS Code, Teams, Spotify and other programs – sign-in and settings stay, the programs rebuild the cache", L"Caches de Discord, Slack, Steam, VS Code, Teams, Spotify et d’autres programmes – connexion et paramètres sont conservés, les programmes reconstruisent le cache", L"Cachés de Discord, Slack, Steam, VS Code, Teams, Spotify y otros programas: el inicio de sesión y los ajustes se conservan, los programas reconstruyen la caché", L"Pamięci podręczne Discord, Slack, Steam, VS Code, Teams, Spotify i innych programów – logowanie i ustawienia zostają, programy odbudowują pamięć podręczną", L"Caches de Discord, Slack, Steam, VS Code, Teams, Spotify e outros programas – login e configurações permanecem, os programas recriam o cache", L"Discord、Slack、Steam、VS Code、Teams、Spotify などのキャッシュ – サインインと設定は残り、各プログラムがキャッシュを再作成します", L"Discord、Slack、Steam、VS Code、Teams、Spotify 等程式的快取 – 登入與設定會保留，程式會重新建立快取"}},
    // Entwickler-Caches und Windows-Protokolle (1.6.0)
    {L"Entwickler-Caches (npm, pip, NuGet, Yarn)",
     {L"Developer caches (npm, pip, NuGet, Yarn)", L"Caches de développement (npm, pip, NuGet, Yarn)", L"Cachés de desarrollo (npm, pip, NuGet, Yarn)", L"Pamięci podręczne programisty (npm, pip, NuGet, Yarn)", L"Caches de desenvolvimento (npm, pip, NuGet, Yarn)", L"開発者向けキャッシュ (npm, pip, NuGet, Yarn)", L"開發人員快取 (npm, pip, NuGet, Yarn)"}},
    {L"Heruntergeladene Paket-Zwischenspeicher von npm, pip, NuGet und Yarn – werden bei Bedarf neu geladen, das nächste Installieren dauert dann etwas länger",
     {L"Downloaded package caches of npm, pip, NuGet and Yarn – they are fetched again when needed, so the next install takes a little longer", L"Caches de paquets téléchargés de npm, pip, NuGet et Yarn – ils sont retéléchargés au besoin, la prochaine installation sera un peu plus longue", L"Cachés de paquetes descargados de npm, pip, NuGet y Yarn: se vuelven a descargar cuando hace falta, la próxima instalación tardará un poco más", L"Pobrane pamięci podręczne pakietów npm, pip, NuGet i Yarn – są pobierane ponownie w razie potrzeby, następna instalacja potrwa nieco dłużej", L"Caches de pacotes baixados de npm, pip, NuGet e Yarn – são baixados novamente quando necessário, a próxima instalação demora um pouco mais", L"npm、pip、NuGet、Yarn のダウンロード済みパッケージキャッシュ – 必要に応じて再取得されるため、次回のインストールは少し時間がかかります", L"npm、pip、NuGet 與 Yarn 已下載的套件快取 – 需要時會重新下載，下次安裝會稍慢一些"}},
    {L"Ablaufverfolgungs-Protokolle (.etl) und Wartungsprotokolle (CBS, DISM) von Windows. Von Windows benutzte Dateien bleiben (ohne Administratorrechte)",
     {L"Windows trace logs (.etl) and maintenance logs (CBS, DISM). Files in use by Windows stay (without administrator rights)", L"Journaux de suivi Windows (.etl) et journaux de maintenance (CBS, DISM). Les fichiers utilisés par Windows restent (sans droits administrateur)", L"Registros de seguimiento de Windows (.etl) y de mantenimiento (CBS, DISM). Los archivos en uso por Windows se conservan (sin derechos de administrador)", L"Dzienniki śledzenia systemu Windows (.etl) i dzienniki konserwacji (CBS, DISM). Pliki używane przez system Windows pozostają (bez uprawnień administratora)", L"Logs de rastreamento do Windows (.etl) e logs de manutenção (CBS, DISM). Arquivos em uso pelo Windows permanecem (sem direitos de administrador)", L"Windows のトレースログ (.etl) とメンテナンスログ (CBS、DISM)。Windows が使用中のファイルは残ります (管理者権限なしの場合)", L"Windows 追蹤記錄 (.etl) 與維護記錄 (CBS、DISM)。Windows 使用中的檔案會保留 (沒有系統管理員權限時)"}},
    // Doppelte Dateien (1.4.0)
    {L"Doppelte Dateien",
     {L"Duplicate files", L"Fichiers en double", L"Archivos duplicados", L"Zduplikowane pliki", L"Arquivos duplicados", L"重複ファイル", L"重複檔案"}},
    {L"Inhaltlich gleiche Dateien – die älteste Kopie bleibt, die übrigen kommen in den Papierkorb",
     {L"Files with identical content – the oldest copy stays, the others go to the Recycle Bin", L"Fichiers au contenu identique – la copie la plus ancienne reste, les autres vont dans la corbeille", L"Archivos con el mismo contenido: se conserva la copia más antigua y las demás van a la papelera", L"Pliki o identycznej zawartości – najstarsza kopia zostaje, pozostałe trafiają do kosza", L"Arquivos com conteúdo idêntico – a cópia mais antiga fica, as demais vão para a Lixeira", L"内容が同じファイル – 最も古いコピーは残り、他はごみ箱に移動します", L"內容相同的檔案 – 保留最舊的副本，其餘移至資源回收筒"}},
    {L"Doppelte Downloads erkennen",
     {L"Detect duplicate downloads", L"Détecter les téléchargements en double", L"Detectar descargas duplicadas", L"Wykrywaj zduplikowane pliki do pobrania", L"Detectar downloads duplicados", L"ダウンロードの重複を検出", L"偵測重複的下載檔案"}},
    {L"Sucht im Downloads-Ordner nach Dateien mit gleichem Inhalt (nicht nur gleichem Namen) und zeigt die überzähligen Kopien als eigene Gruppe „Doppelte Dateien“. Die älteste Kopie bleibt immer, die übrigen kommen wie alle Downloads in den Papierkorb. Große Downloads brauchen beim Öffnen der Seite etwas länger.",
     {L"Looks in the Downloads folder for files with the same content (not just the same name) and shows the extra copies as a separate group, “Duplicate files”. The oldest copy always stays; the others go to the Recycle Bin like all downloads. Large downloads make the page take a little longer to open.", L"Cherche dans le dossier Téléchargements les fichiers au même contenu (pas seulement au même nom) et affiche les copies en trop dans un groupe à part, « Fichiers en double ». La copie la plus ancienne reste toujours ; les autres vont dans la corbeille comme tous les téléchargements. Les gros téléchargements allongent un peu l’ouverture de la page.", L"Busca en la carpeta Descargas archivos con el mismo contenido (no solo el mismo nombre) y muestra las copias sobrantes como un grupo aparte, «Archivos duplicados». La copia más antigua siempre se conserva; las demás van a la papelera como todas las descargas. Las descargas grandes hacen que la página tarde un poco más en abrirse.", L"Szuka w folderze Pobrane plików o tej samej zawartości (nie tylko o tej samej nazwie) i pokazuje nadmiarowe kopie jako osobną grupę „Zduplikowane pliki”. Najstarsza kopia zawsze zostaje, pozostałe trafiają do kosza jak wszystkie pobrane pliki. Duże pliki wydłużają otwieranie strony.", L"Procura na pasta Downloads arquivos com o mesmo conteúdo (não apenas o mesmo nome) e mostra as cópias extras como um grupo separado, “Arquivos duplicados”. A cópia mais antiga sempre fica; as demais vão para a Lixeira como todos os downloads. Downloads grandes deixam a página um pouco mais lenta para abrir.", L"ダウンロードフォルダー内で、名前だけでなく内容が同じファイルを探し、余分なコピーを「重複ファイル」という別グループで表示します。最も古いコピーは常に残り、他は通常のダウンロードと同様にごみ箱へ移動します。大きなファイルがあるとページを開くのに少し時間がかかります。", L"在「下載」資料夾中尋找內容相同（不只是名稱相同）的檔案，並將多餘的副本顯示為獨立群組「重複檔案」。最舊的副本一定會保留，其餘與一般下載檔案一樣移至資源回收筒。下載檔案很大時，開啟頁面會稍慢一些。"}},
};

// Ausfuehrliche Seitentexte (Klick auf das Info-Symbol): je Seite alle Sprachen in der Reihenfolge von kLangs (de, en, fr, es, pl, pt, ja, zh).
// Seiten: 0 Downloads, 1 Temp, 2 Systemdateien, 3 Browser, 4 Laufwerke, 5 Autostart, 6 Programme
static const wchar_t* const kInfoTexts[7][kLangCount] = {
    {  // Dl
        L"Dein Downloads-Ordner.\n\n"
        L"• Die Einträge sind in Kacheln nach Dateityp gruppiert (Dokumente, Bilder, Videos …). Alle Schalter sind zu Beginn an, Schalter aus = bleibt erhalten. Fährst du mit der Maus über eine Kachel, siehst du Beispiele.\n"
        L"• Gelöschtes landet im Papierkorb und lässt sich wiederherstellen.\n"
        L"• „Alle / Keine“ wählt alles an oder ab.\n"
        L"• „Schutzliste“: Dateinamen oder Muster (z. B. *.pdf), die nie gelöscht werden. Systemdateien wie desktop.ini sind immer geschützt.\n"
        L"• „Auto-Clean“ räumt den Ordner im eingestellten Intervall auf. Intervall und Größengrenze stellst du in den Einstellungen ein.",
        L"Your Downloads folder.\n\n"
        L"• Entries are grouped into tiles by file type (documents, images, videos …). All switches start on; switch off = kept. Hover over a tile to see examples.\n"
        L"• Deleted items go to the Recycle Bin and can be restored.\n"
        L"• \"All / None\" selects or deselects everything.\n"
        L"• \"Protected list\": file names or patterns (e.g. *.pdf) that are never deleted. System files like desktop.ini are always protected.\n"
        L"• \"Auto-Clean\" tidies the folder at the configured interval. Interval and size limit are set in Settings.",
        L"Votre dossier Téléchargements.\n\n"
        L"• Les entrées sont regroupées en tuiles par type de fichier (documents, images, vidéos …). Tous les interrupteurs sont activés au départ ; désactivé = conservé. En survolant une tuile, vous voyez des exemples.\n"
        L"• Ce qui est supprimé va dans la Corbeille et peut être restauré.\n"
        L"• « Tout / Rien » sélectionne ou désélectionne tout.\n"
        L"• « Liste de protection » : noms de fichiers ou motifs (p. ex. *.pdf) qui ne sont jamais supprimés. Les fichiers système comme desktop.ini sont toujours protégés.\n"
        L"• « Auto-Clean » nettoie le dossier à l'intervalle réglé. L'intervalle et la limite de taille se règlent dans les paramètres.",
        L"Tu carpeta Descargas.\n\n"
        L"• Las entradas se agrupan en mosaicos por tipo de archivo (documentos, imágenes, vídeos …). Todos los interruptores empiezan activados; desactivado = se conserva. Al pasar el ratón por un mosaico verás ejemplos.\n"
        L"• Lo que se elimina va a la Papelera y se puede restaurar.\n"
        L"• «Todo / Nada» selecciona o deselecciona todo.\n"
        L"• «Lista de protección»: nombres de archivo o patrones (p. ej. *.pdf) que nunca se eliminan. Los archivos del sistema como desktop.ini siempre están protegidos.\n"
        L"• «Auto-Clean» limpia la carpeta en el intervalo configurado. El intervalo y el límite de tamaño se ajustan en Ajustes.",
        L"Twój folder Pobrane.\n\n"
        L"• Wpisy są pogrupowane w kafelki według typu pliku (dokumenty, obrazy, wideo …). Wszystkie przełączniki są na początku włączone; wyłączony = zostaje zachowane. Po najechaniu na kafelek zobaczysz przykłady.\n"
        L"• Usunięte pliki trafiają do Kosza i można je przywrócić.\n"
        L"• „Wszystko / Nic” zaznacza lub odznacza wszystko.\n"
        L"• „Lista ochrony”: nazwy plików lub wzorce (np. *.pdf), które nigdy nie są usuwane. Pliki systemowe, takie jak desktop.ini, są zawsze chronione.\n"
        L"• „Auto-Clean” porządkuje folder w ustawionym odstępie. Odstęp i limit rozmiaru ustawiasz w Ustawieniach.",
        L"Sua pasta Downloads.\n\n"
        L"• As entradas são agrupadas em blocos por tipo de arquivo (documentos, imagens, vídeos …). Todas as chaves começam ligadas; desligada = é mantido. Passe o mouse sobre um bloco para ver exemplos.\n"
        L"• O que é excluído vai para a Lixeira e pode ser restaurado.\n"
        L"• «Todos / Nenhum» marca ou desmarca tudo.\n"
        L"• «Lista de proteção»: nomes de arquivo ou padrões (p. ex. *.pdf) que nunca são excluídos. Arquivos do sistema como desktop.ini são sempre protegidos.\n"
        L"• O «Auto-Clean» limpa a pasta no intervalo configurado. Intervalo e limite de tamanho são definidos em Configurações.",
        L"ダウンロードフォルダーです。\n\n"
        L"• 項目はファイルの種類ごとにタイルでまとめて表示されます (ドキュメント、画像、動画 …)。スイッチは最初すべてオンで、オフにすると保持されます。タイルにマウスを合わせると例が表示されます。\n"
        L"• 削除したものはごみ箱に入り、元に戻せます。\n"
        L"• 「すべて / なし」で、全部を選択または解除します。\n"
        L"• 「保護リスト」：削除されないファイル名やパターン (例： *.pdf)。desktop.ini などのシステムファイルは常に保護されます。\n"
        L"• 「Auto-Clean」は、設定した間隔でフォルダーを整理します。間隔とサイズの上限は設定で変更できます。",
        L"你的「下載」資料夾。\n\n"
        L"• 項目會依檔案類型以磚塊分組 (文件、圖片、影片 …)。所有開關一開始都是開啟，關閉 = 會保留。將滑鼠移到磚塊上可看到範例。\n"
        L"• 刪除的內容會進入資源回收筒，可以還原。\n"
        L"• 「全選 / 全不選」會選取或取消選取全部。\n"
        L"• 「保護清單」：永遠不會被刪除的檔名或比對模式 (例如 *.pdf)。desktop.ini 等系統檔案一律受保護。\n"
        L"• 「Auto-Clean」會依設定的間隔整理資料夾。間隔與大小上限可在設定中調整。"
    },
    {  // Temp
        L"Temporäre Dateien deines Benutzerkontos (AppData\\Local\\Temp).\n\n"
        L"• Die Einträge sind in Kacheln nach Art gruppiert (Programm-Ordner, .tmp-Dateien, Protokolle …). Fährst du mit der Maus über eine Kachel, siehst du Beispiele.\n"
        L"• Schalter an = wird mit „Clean Now“ gelöscht, Schalter aus = bleibt erhalten.\n"
        L"• Das sind Zwischendateien von Programmen. Windows legt sie bei Bedarf neu an.\n"
        L"• Sie werden endgültig gelöscht, nicht in den Papierkorb verschoben.\n"
        L"• Dateien, die gerade ein Programm benutzt, bleiben automatisch erhalten.\n"
        L"• Einzelne Dateien schützt du dauerhaft mit der „Schutzliste“.",
        L"Temporary files of your user account (AppData\\Local\\Temp).\n\n"
        L"• Entries are grouped into tiles by type (program folders, .tmp files, logs …). Hover over a tile to see examples.\n"
        L"• Switch on = deleted with \"Clean Now\", switch off = kept.\n"
        L"• These are intermediate files of programs. Windows recreates them when needed.\n"
        L"• They are deleted permanently, not moved to the Recycle Bin.\n"
        L"• Files that a program is currently using are kept automatically.\n"
        L"• Protect single files permanently with \"Protected list\".",
        L"Fichiers temporaires de votre compte utilisateur (AppData\\Local\\Temp).\n\n"
        L"• Les entrées sont regroupées en tuiles par type (dossiers de programmes, fichiers .tmp, journaux …). En survolant une tuile, vous voyez des exemples.\n"
        L"• Interrupteur activé = supprimé avec « Nettoyer maintenant », désactivé = conservé.\n"
        L"• Ce sont des fichiers intermédiaires de programmes. Windows les recrée au besoin.\n"
        L"• Ils sont supprimés définitivement, pas déplacés dans la Corbeille.\n"
        L"• Les fichiers qu'un programme utilise en ce moment sont conservés automatiquement.\n"
        L"• Protégez des fichiers individuels durablement avec la « Liste de protection ».",
        L"Archivos temporales de tu cuenta de usuario (AppData\\Local\\Temp).\n\n"
        L"• Las entradas se agrupan en mosaicos por tipo (carpetas de programas, archivos .tmp, registros …). Al pasar el ratón por un mosaico verás ejemplos.\n"
        L"• Interruptor activado = se elimina con «Limpiar ahora», desactivado = se conserva.\n"
        L"• Son archivos intermedios de programas. Windows los vuelve a crear cuando hace falta.\n"
        L"• Se eliminan definitivamente, no se mueven a la Papelera.\n"
        L"• Los archivos que un programa está usando se conservan automáticamente.\n"
        L"• Protege archivos individuales de forma permanente con la «Lista de protección».",
        L"Pliki tymczasowe Twojego konta użytkownika (AppData\\Local\\Temp).\n\n"
        L"• Wpisy są pogrupowane w kafelki według rodzaju (foldery programów, pliki .tmp, dzienniki …). Po najechaniu na kafelek zobaczysz przykłady.\n"
        L"• Przełącznik włączony = zostanie usunięte przez „Wyczyść teraz”, wyłączony = zostaje zachowane.\n"
        L"• To pliki pośrednie programów. Windows tworzy je ponownie, gdy są potrzebne.\n"
        L"• Są usuwane na stałe, nie przenoszone do Kosza.\n"
        L"• Pliki, których program właśnie używa, są automatycznie zachowywane.\n"
        L"• Pojedyncze pliki chronisz na stałe za pomocą „Listy ochrony”.",
        L"Arquivos temporários da sua conta de usuário (AppData\\Local\\Temp).\n\n"
        L"• As entradas são agrupadas em blocos por tipo (pastas de programas, arquivos .tmp, logs …). Passe o mouse sobre um bloco para ver exemplos.\n"
        L"• Chave ligada = é excluído com «Limpar agora», desligada = é mantido.\n"
        L"• São arquivos intermediários de programas. O Windows os recria quando necessário.\n"
        L"• São excluídos definitivamente, não movidos para a Lixeira.\n"
        L"• Arquivos que um programa está usando no momento são mantidos automaticamente.\n"
        L"• Proteja arquivos individuais de forma permanente com a «Lista de proteção».",
        L"ユーザーアカウントの一時ファイルです (AppData\\Local\\Temp)。\n\n"
        L"• 項目は種類ごとにタイルでまとめて表示されます (プログラムのフォルダー、.tmp ファイル、ログ …)。タイルにマウスを合わせると例が表示されます。\n"
        L"• スイッチがオン = 「今すぐクリーン」で削除、オフ = 保持されます。\n"
        L"• プログラムの中間ファイルです。必要になると Windows が再作成します。\n"
        L"• ごみ箱には移動せず、完全に削除されます。\n"
        L"• プログラムが現在使用中のファイルは自動的に保持されます。\n"
        L"• 個々のファイルは「保護リスト」で恒久的に守れます。",
        L"你使用者帳戶的暫存檔案 (AppData\\Local\\Temp)。\n\n"
        L"• 項目會依類型以磚塊分組 (程式資料夾、.tmp 檔案、記錄 …)。將滑鼠移到磚塊上可看到範例。\n"
        L"• 開關開啟 = 會用「立即清理」刪除，關閉 = 會保留。\n"
        L"• 這些是程式的中繼檔案。Windows 需要時會重新建立。\n"
        L"• 它們會被永久刪除，不會移到資源回收筒。\n"
        L"• 程式目前正在使用的檔案會自動保留。\n"
        L"• 個別檔案可用「保護清單」永久保護。"
    },
    {  // Sys
        L"Aufräumbares von Windows in deinem Benutzerkonto.\n\n"
        L"• Die Einträge sind in Kacheln nach Art gruppiert. Schalter an = wird mit „Clean Now“ gelöscht. Fährst du mit der Maus über eine Kachel, siehst du Beispiele.\n"
        L"• Vorschaubild- und Symbol-Cache (thumbcache, iconcache): Der Explorer baut ihn neu auf.\n"
        L"• Absturzberichte und Fehlerprotokolle (CrashDumps, Windows-Fehlerberichte).\n"
        L"• Wird endgültig gelöscht. Benutzte Dateien bleiben erhalten.\n"
        L"• Es werden keine Windows-Systemdateien außerhalb deines Benutzerordners angefasst.",
        L"Things Windows leaves behind in your user account that can be cleaned up.\n\n"
        L"• Entries are grouped into tiles by type. Switch on = deleted with \"Clean Now\". Hover over a tile to see examples.\n"
        L"• Thumbnail and icon cache (thumbcache, iconcache): Explorer rebuilds it.\n"
        L"• Crash reports and error logs (CrashDumps, Windows error reports).\n"
        L"• Deleted permanently. Files in use are kept.\n"
        L"• No Windows system files outside your user folder are touched.",
        L"Ce que Windows laisse dans votre compte utilisateur et qui peut être nettoyé.\n\n"
        L"• Les entrées sont regroupées en tuiles par type. Interrupteur activé = supprimé avec « Nettoyer maintenant ». En survolant une tuile, vous voyez des exemples.\n"
        L"• Cache des miniatures et des icônes (thumbcache, iconcache) : l'Explorateur le reconstruit.\n"
        L"• Rapports de plantage et journaux d'erreurs (CrashDumps, rapports d'erreurs Windows).\n"
        L"• Supprimé définitivement. Les fichiers utilisés sont conservés.\n"
        L"• Aucun fichier système Windows en dehors de votre dossier utilisateur n'est touché.",
        L"Lo que Windows deja en tu cuenta de usuario y se puede limpiar.\n\n"
        L"• Las entradas se agrupan en mosaicos por tipo. Interruptor activado = se elimina con «Limpiar ahora». Al pasar el ratón por un mosaico verás ejemplos.\n"
        L"• Caché de miniaturas e iconos (thumbcache, iconcache): el Explorador la reconstruye.\n"
        L"• Informes de fallos y registros de errores (CrashDumps, informes de errores de Windows).\n"
        L"• Se elimina definitivamente. Los archivos en uso se conservan.\n"
        L"• No se toca ningún archivo del sistema de Windows fuera de tu carpeta de usuario.",
        L"To, co Windows zostawia na Twoim koncie użytkownika i co można wyczyścić.\n\n"
        L"• Wpisy są pogrupowane w kafelki według rodzaju. Przełącznik włączony = zostanie usunięte przez „Wyczyść teraz”. Po najechaniu na kafelek zobaczysz przykłady.\n"
        L"• Pamięć miniatur i ikon (thumbcache, iconcache): Eksplorator odbuduje ją na nowo.\n"
        L"• Raporty awarii i dzienniki błędów (CrashDumps, raporty błędów Windows).\n"
        L"• Usuwane na stałe. Używane pliki zostają zachowane.\n"
        L"• Żadne pliki systemowe Windows poza Twoim folderem użytkownika nie są ruszane.",
        L"O que o Windows deixa na sua conta de usuário e pode ser limpo.\n\n"
        L"• As entradas são agrupadas em blocos por tipo. Chave ligada = é excluído com «Limpar agora». Passe o mouse sobre um bloco para ver exemplos.\n"
        L"• Cache de miniaturas e ícones (thumbcache, iconcache): o Explorer o reconstrói.\n"
        L"• Relatórios de falhas e logs de erros (CrashDumps, relatórios de erros do Windows).\n"
        L"• Excluído definitivamente. Arquivos em uso são mantidos.\n"
        L"• Nenhum arquivo de sistema do Windows fora da sua pasta de usuário é tocado.",
        L"Windows があなたのユーザーアカウントに残した、整理できるものです。\n\n"
        L"• 項目は種類ごとにタイルでまとめて表示されます。スイッチがオン = 「今すぐクリーン」で削除されます。タイルにマウスを合わせると例が表示されます。\n"
        L"• サムネイルとアイコンのキャッシュ (thumbcache、iconcache)：エクスプローラーが再作成します。\n"
        L"• クラッシュレポートとエラーログ (CrashDumps、Windows エラーレポート)。\n"
        L"• 完全に削除されます。使用中のファイルは保持されます。\n"
        L"• ユーザーフォルダー以外の Windows のシステムファイルには触れません。",
        L"Windows 在你的使用者帳戶中留下、可以清理的項目。\n\n"
        L"• 項目會依類型以磚塊分組。開關開啟 = 會用「立即清理」刪除。將滑鼠移到磚塊上可看到範例。\n"
        L"• 縮圖與圖示快取 (thumbcache、iconcache)：檔案總管會重新建立。\n"
        L"• 當機報告與錯誤記錄 (CrashDumps、Windows 錯誤報告)。\n"
        L"• 會永久刪除。使用中的檔案會保留。\n"
        L"• 不會動到你使用者資料夾以外的 Windows 系統檔案。"
    },
    {  // Browser
        L"Hier siehst du jeden Browser mit der Größe seines Caches.\n\n"
        L"• Der Cache besteht aus zwischengespeicherten Daten (Bilder, Skripte, Webseiten-Teile). Er baut sich beim Surfen neu auf; Seiten laden danach nur einmal etwas langsamer.\n"
        L"• Cookies, Passwörter, Verlauf, Lesezeichen und Logins bleiben erhalten.\n"
        L"• Chrome, Edge, Brave, Opera GX: die Ordner „Cache“, „Code Cache“ und „GPUCache“ jedes Profils. Firefox: „cache2“.\n"
        L"• Schalter an = wird mit „Clean Now“ endgültig gelöscht.\n"
        L"• Läuft ein Browser, bleiben benutzte Dateien erhalten. Am besten vorher schließen.\n"
        L"• Fährst du mit der Maus über eine Zeile, siehst du die genauen Ordner.",
        L"Here you can see each browser with the size of its cache.\n\n"
        L"• The cache consists of stored data (images, scripts, parts of web pages). It rebuilds itself while you browse; pages only load a little slower the first time afterwards.\n"
        L"• Cookies, passwords, history, bookmarks and logins are kept.\n"
        L"• Chrome, Edge, Brave, Opera GX: the \"Cache\", \"Code Cache\" and \"GPUCache\" folders of every profile. Firefox: \"cache2\".\n"
        L"• Switch on = permanently deleted with \"Clean Now\".\n"
        L"• If a browser is running, files in use are kept. It is best to close it first.\n"
        L"• Hover over a row to see the exact folders.",
        L"Vous voyez ici chaque navigateur avec la taille de son cache.\n\n"
        L"• Le cache contient des données mises en mémoire (images, scripts, parties de pages web). Il se reconstruit en naviguant ; les pages se chargent ensuite un peu plus lentement une seule fois.\n"
        L"• Cookies, mots de passe, historique, favoris et connexions sont conservés.\n"
        L"• Chrome, Edge, Brave, Opera GX : les dossiers « Cache », « Code Cache » et « GPUCache » de chaque profil. Firefox : « cache2 ».\n"
        L"• Interrupteur activé = supprimé définitivement avec « Nettoyer maintenant ».\n"
        L"• Si un navigateur est ouvert, les fichiers utilisés sont conservés. Mieux vaut le fermer avant.\n"
        L"• En survolant une ligne, vous voyez les dossiers exacts.",
        L"Aquí ves cada navegador con el tamaño de su caché.\n\n"
        L"• La caché contiene datos almacenados (imágenes, scripts, partes de páginas web). Se reconstruye al navegar; después las páginas solo cargan un poco más despacio la primera vez.\n"
        L"• Las cookies, contraseñas, historial, marcadores e inicios de sesión se conservan.\n"
        L"• Chrome, Edge, Brave, Opera GX: las carpetas «Cache», «Code Cache» y «GPUCache» de cada perfil. Firefox: «cache2».\n"
        L"• Interruptor activado = se elimina definitivamente con «Limpiar ahora».\n"
        L"• Si un navegador está en ejecución, los archivos en uso se conservan. Lo mejor es cerrarlo antes.\n"
        L"• Al pasar el ratón por una fila verás las carpetas exactas.",
        L"Tutaj widzisz każdą przeglądarkę z rozmiarem jej pamięci podręcznej.\n\n"
        L"• Pamięć podręczna to zapisane dane (obrazy, skrypty, fragmenty stron). Odbudowuje się podczas przeglądania; strony ładują się potem tylko raz nieco wolniej.\n"
        L"• Ciasteczka, hasła, historia, zakładki i logowania zostają zachowane.\n"
        L"• Chrome, Edge, Brave, Opera GX: foldery „Cache”, „Code Cache” i „GPUCache” każdego profilu. Firefox: „cache2”.\n"
        L"• Przełącznik włączony = usuwane na stałe przez „Wyczyść teraz”.\n"
        L"• Jeśli przeglądarka działa, używane pliki zostają zachowane. Najlepiej zamknąć ją wcześniej.\n"
        L"• Po najechaniu na wiersz zobaczysz dokładne foldery.",
        L"Aqui você vê cada navegador com o tamanho do seu cache.\n\n"
        L"• O cache contém dados armazenados (imagens, scripts, partes de páginas da web). Ele se reconstrói enquanto você navega; depois, as páginas só carregam um pouco mais devagar na primeira vez.\n"
        L"• Cookies, senhas, histórico, favoritos e logins são mantidos.\n"
        L"• Chrome, Edge, Brave, Opera GX: as pastas «Cache», «Code Cache» e «GPUCache» de cada perfil. Firefox: «cache2».\n"
        L"• Chave ligada = excluído definitivamente com «Limpar agora».\n"
        L"• Se um navegador estiver em execução, os arquivos em uso são mantidos. O melhor é fechá-lo antes.\n"
        L"• Passe o mouse sobre uma linha para ver as pastas exatas.",
        L"各ブラウザーとそのキャッシュの大きさを表示します。\n\n"
        L"• キャッシュは保存されたデータです (画像、スクリプト、Web ページの一部)。閲覧中に再び作られ、そのあとは最初の 1 回だけページの読み込みがやや遅くなります。\n"
        L"• Cookie、パスワード、履歴、ブックマーク、ログイン情報は保持されます。\n"
        L"• Chrome、Edge、Brave、Opera GX：各プロファイルの「Cache」「Code Cache」「GPUCache」フォルダー。Firefox：「cache2」。\n"
        L"• スイッチがオン = 「今すぐクリーン」で完全に削除されます。\n"
        L"• ブラウザーが起動中の場合、使用中のファイルは保持されます。先に閉じるのがおすすめです。\n"
        L"• 行にマウスを合わせると、正確なフォルダーが表示されます。",
        L"這裡會顯示每個瀏覽器及其快取大小。\n\n"
        L"• 快取是儲存的資料 (圖片、指令碼、網頁片段)。瀏覽時會自動重建；之後頁面只會在第一次稍微慢一點。\n"
        L"• Cookie、密碼、瀏覽記錄、書籤與登入狀態會保留。\n"
        L"• Chrome、Edge、Brave、Opera GX：每個設定檔的「Cache」、「Code Cache」與「GPUCache」資料夾。Firefox：「cache2」。\n"
        L"• 開關開啟 = 會用「立即清理」永久刪除。\n"
        L"• 瀏覽器執行中時，使用中的檔案會保留。最好先關閉。\n"
        L"• 將滑鼠移到某一列上，可看到確切的資料夾。"
    },
    {  // Drive
        L"Hier siehst du alle Laufwerke mit Belegung und Zustand.\n\n"
        L"• Der Balken zeigt, wie voll das Laufwerk ist (blau, ab 75 % gelb, ab 90 % rot).\n"
        L"• Gesundheit: der Status, den Windows für die Platte meldet (Gesund, Warnung, Fehlerhaft). Dazu, wenn verfügbar: Temperatur, Abnutzung bei SSDs, Betriebsstunden sowie Lese- und Schreibfehler.\n"
        L"• „Nicht verfügbar“ heißt nur, dass das Laufwerk diese Werte nicht herausgibt (typisch bei USB-Sticks und vielen externen Platten). Es bedeutet nicht, dass es defekt ist.\n"
        L"• Einzelne SMART-Werte und defekte Sensoren lassen sich so nicht prüfen. Bei einer Warnung bitte Daten sichern und ein Spezialprogramm wie CrystalDiskInfo nutzen.\n"
        L"• Hier wird nur gelesen, nichts verändert oder gelöscht.\n"
        L"• Fährst du mit der Maus über eine Karte, siehst du alle Einzelheiten.",
        L"Here you can see all drives with usage and health.\n\n"
        L"• The bar shows how full the drive is (blue, yellow above 75 %, red above 90 %).\n"
        L"• Health: the status Windows reports for the disk (Healthy, Warning, Unhealthy). Also, when available: temperature, SSD wear, power-on hours, and read and write errors.\n"
        L"• \"Not available\" only means the drive does not report these values (typical for USB sticks and many external drives). It does not mean the drive is faulty.\n"
        L"• Individual SMART values and faulty sensors cannot be checked this way. On a warning, please back up your data and use a dedicated tool such as CrystalDiskInfo.\n"
        L"• This page is read-only; nothing is changed or deleted.\n"
        L"• Hover over a card to see all details.",
        L"Vous voyez ici tous les lecteurs avec leur occupation et leur état.\n\n"
        L"• La barre montre le remplissage du lecteur (bleu, jaune dès 75 %, rouge dès 90 %).\n"
        L"• Santé : l'état que Windows indique pour le disque (Bon, Avertissement, Défectueux). Si disponibles : température, usure des SSD, heures de fonctionnement ainsi qu'erreurs de lecture et d'écriture.\n"
        L"• « Indisponible » signifie seulement que le lecteur ne fournit pas ces valeurs (typique des clés USB et de nombreux disques externes). Cela ne signifie pas qu'il est défectueux.\n"
        L"• Les valeurs SMART individuelles et les capteurs défectueux ne peuvent pas être vérifiés ainsi. En cas d'avertissement, sauvegardez vos données et utilisez un outil spécialisé comme CrystalDiskInfo.\n"
        L"• Cette page est en lecture seule, rien n'est modifié ni supprimé.\n"
        L"• En survolant une carte, vous voyez tous les détails.",
        L"Aquí ves todas las unidades con su ocupación y estado.\n\n"
        L"• La barra muestra lo llena que está la unidad (azul, amarillo desde el 75 %, rojo desde el 90 %).\n"
        L"• Salud: el estado que Windows indica para el disco (Correcto, Advertencia, Defectuoso). Si están disponibles: temperatura, desgaste en SSD, horas de funcionamiento y errores de lectura y escritura.\n"
        L"• «No disponible» solo significa que la unidad no ofrece esos valores (típico de memorias USB y muchos discos externos). No significa que esté averiada.\n"
        L"• Los valores SMART individuales y los sensores defectuosos no se pueden comprobar así. Ante una advertencia, haz una copia de seguridad y usa un programa especializado como CrystalDiskInfo.\n"
        L"• Esta página es de solo lectura; no se cambia ni se elimina nada.\n"
        L"• Al pasar el ratón por una tarjeta verás todos los detalles.",
        L"Tutaj widzisz wszystkie dyski wraz z zajętością i stanem.\n\n"
        L"• Pasek pokazuje, jak pełny jest dysk (niebieski, od 75 % żółty, od 90 % czerwony).\n"
        L"• Stan: status, który Windows podaje dla dysku (Dobry, Ostrzeżenie, Uszkodzony). Jeśli są dostępne: temperatura, zużycie SSD, godziny pracy oraz błędy odczytu i zapisu.\n"
        L"• „Niedostępne” oznacza tylko, że dysk nie podaje tych wartości (typowe dla pamięci USB i wielu dysków zewnętrznych). Nie znaczy, że jest uszkodzony.\n"
        L"• Pojedynczych wartości SMART i uszkodzonych czujników w ten sposób nie sprawdzisz. Przy ostrzeżeniu zrób kopię danych i użyj specjalnego programu, np. CrystalDiskInfo.\n"
        L"• Ta strona tylko odczytuje, nic nie jest zmieniane ani usuwane.\n"
        L"• Po najechaniu na kartę zobaczysz wszystkie szczegóły.",
        L"Aqui você vê todas as unidades com ocupação e integridade.\n\n"
        L"• A barra mostra o quanto a unidade está cheia (azul, amarelo a partir de 75 %, vermelho a partir de 90 %).\n"
        L"• Integridade: o status que o Windows informa para o disco (Íntegro, Aviso, Com defeito). Se disponíveis: temperatura, desgaste de SSDs, horas de funcionamento e erros de leitura e gravação.\n"
        L"• «Indisponível» só significa que a unidade não informa esses valores (típico de pen drives USB e muitos discos externos). Não significa que ela esteja com defeito.\n"
        L"• Valores SMART individuais e sensores com defeito não podem ser verificados assim. Em caso de aviso, faça backup dos dados e use um programa especializado como o CrystalDiskInfo.\n"
        L"• Esta página só lê; nada é alterado ou excluído.\n"
        L"• Passe o mouse sobre um cartão para ver todos os detalhes.",
        L"すべてのドライブの使用状況と状態を表示します。\n\n"
        L"• バーはドライブの使用量を示します (青、75 % 以上で黄、90 % 以上で赤)。\n"
        L"• 正常性：Windows がディスクについて報告する状態です (正常、警告、異常)。利用できる場合は、温度、SSD の消耗、稼働時間、読み取り・書き込みエラーも表示します。\n"
        L"• 「利用できません」は、ドライブがこれらの値を提供しないというだけの意味です (USB メモリや多くの外付けドライブで一般的)。故障しているという意味ではありません。\n"
        L"• 個々の SMART 値や故障したセンサーは、この方法では確認できません。警告が出たらデータをバックアップし、CrystalDiskInfo などの専用ツールを使ってください。\n"
        L"• このページは読み取り専用で、何も変更・削除されません。\n"
        L"• カードにマウスを合わせると、すべての詳細が表示されます。",
        L"這裡會顯示所有磁碟機的使用量與狀態。\n\n"
        L"• 長條顯示磁碟機的使用程度 (藍色，75 % 以上為黃色，90 % 以上為紅色)。\n"
        L"• 健康狀態：Windows 回報的磁碟狀態 (良好、警告、異常)。若有提供，也會顯示溫度、SSD 損耗、運作時數，以及讀取與寫入錯誤。\n"
        L"• 「無法取得」只代表磁碟機沒有提供這些數值 (USB 隨身碟與許多外接硬碟常見)，並不代表它已損壞。\n"
        L"• 個別 SMART 數值與故障的感測器無法用這種方式檢查。出現警告時，請備份資料，並使用 CrystalDiskInfo 等專用工具。\n"
        L"• 此頁面僅供讀取，不會變更或刪除任何內容。\n"
        L"• 將滑鼠移到卡片上，可看到所有詳細資料。"
    },
    {  // Auto
        L"Ein Programm im Autostart startet automatisch mit Windows.\n\n"
        L"• Schalter aus = startet nicht mehr mit Windows. Es wird nichts gelöscht.\n"
        L"• Du kannst es jederzeit wieder einschalten, auch im Task-Manager unter „Autostart“.\n"
        L"• Mit „Änderungen übernehmen“ speichern; es gilt ab dem nächsten Start von Windows.\n"
        L"• Ein Klick auf eine Kachel zeigt Pfad und Herausgeber des Programms.\n"
        L"• Die Startauswirkung (Niedrig, Mittel, Hoch) ist eine grobe Schätzung nach Programmgröße, keine Messung.",
        L"A program in Autostart starts automatically with Windows.\n\n"
        L"• Switch off = no longer starts with Windows. Nothing is deleted.\n"
        L"• You can switch it back on at any time, also in Task Manager under \"Startup\".\n"
        L"• Save with \"Apply changes\"; it takes effect at the next Windows start.\n"
        L"• A click on a tile shows the path and publisher of the program.\n"
        L"• The startup impact (Low, Medium, High) is a rough estimate based on program size, not a measurement.",
        L"Un programme dans Démarrage se lance automatiquement avec Windows.\n\n"
        L"• Interrupteur désactivé = ne démarre plus avec Windows. Rien n'est supprimé.\n"
        L"• Vous pouvez le réactiver à tout moment, aussi dans le Gestionnaire des tâches sous « Démarrage ».\n"
        L"• Enregistrez avec « Appliquer les modifications » ; effet au prochain démarrage de Windows.\n"
        L"• Un clic sur une tuile affiche le chemin et l'éditeur du programme.\n"
        L"• L'impact au démarrage (Faible, Moyen, Élevé) est une estimation approximative selon la taille du programme, pas une mesure.",
        L"Un programa en Inicio automático se inicia solo con Windows.\n\n"
        L"• Interruptor desactivado = ya no se inicia con Windows. No se elimina nada.\n"
        L"• Puedes volver a activarlo en cualquier momento, también en el Administrador de tareas, en «Inicio».\n"
        L"• Guarda con «Aplicar cambios»; se aplica en el próximo inicio de Windows.\n"
        L"• Un clic en un mosaico muestra la ruta y el editor del programa.\n"
        L"• El impacto en el inicio (Bajo, Medio, Alto) es una estimación aproximada según el tamaño del programa, no una medición.",
        L"Program w autostarcie uruchamia się automatycznie z Windows.\n\n"
        L"• Przełącznik wyłączony = nie uruchamia się już z Windows. Nic nie jest usuwane.\n"
        L"• Możesz go w każdej chwili włączyć ponownie, także w Menedżerze zadań w zakładce „Autostart”.\n"
        L"• Zapisz przez „Zastosuj zmiany”; zmiana działa od następnego uruchomienia Windows.\n"
        L"• Kliknięcie kafelka pokazuje ścieżkę i wydawcę programu.\n"
        L"• Wpływ na uruchamianie (Niski, Średni, Wysoki) to przybliżony szacunek według rozmiaru programu, a nie pomiar.",
        L"Um programa na Inicialização inicia automaticamente com o Windows.\n\n"
        L"• Chave desligada = não inicia mais com o Windows. Nada é excluído.\n"
        L"• Você pode ligá-lo de novo a qualquer momento, também no Gerenciador de Tarefas, em «Inicialização».\n"
        L"• Salve com «Aplicar alterações»; vale a partir da próxima inicialização do Windows.\n"
        L"• Um clique em um bloco mostra o caminho e o editor do programa.\n"
        L"• O impacto na inicialização (Baixo, Médio, Alto) é uma estimativa aproximada pelo tamanho do programa, não uma medição.",
        L"スタートアップのプログラムは、Windows と一緒に自動で起動します。\n\n"
        L"• スイッチがオフ = Windows と一緒に起動しません。何も削除されません。\n"
        L"• いつでもオンに戻せます。タスクマネージャーの「スタートアップ」からも変更できます。\n"
        L"• 「変更を適用」で保存します。次回の Windows 起動から有効になります。\n"
        L"• タイルをクリックすると、プログラムのパスと発行元が表示されます。\n"
        L"• 起動への影響 (低、中、高) は、プログラムのサイズによる大まかな推定であり、実測ではありません。",
        L"開機啟動中的程式會隨 Windows 自動啟動。\n\n"
        L"• 開關關閉 = 不再隨 Windows 啟動。不會刪除任何內容。\n"
        L"• 你隨時可以重新開啟，也可在工作管理員的「啟動」中操作。\n"
        L"• 用「套用變更」儲存；下次啟動 Windows 時生效。\n"
        L"• 點一下磚塊會顯示程式的路徑與發行者。\n"
        L"• 啟動影響 (低、中、高) 是依程式大小做的粗略估計，並非實際量測。"
    },
    {  // Programme
        L"Alle installierten Programme mit Herausgeber und Größe.\n\n"
        L"• „Deinstallieren“ startet das Deinstallationsprogramm des Programms selbst. Dort kannst du noch abbrechen.\n"
        L"• Die Größe stammt aus den Angaben des Programms und kann fehlen oder abweichen.\n"
        L"• Microsoft-Store-Apps und Windows-Komponenten werden hier nicht angezeigt.\n"
        L"• Reste in der Registry oder auf der Festplatte werden nicht angefasst, damit nichts kaputtgeht.\n"
        L"• Die Liste aktualisiert sich, wenn du zum Fenster zurückkehrst.",
        L"All installed programs with publisher and size.\n\n"
        L"• “Uninstall” starts the program’s own uninstaller. You can still cancel there.\n"
        L"• The size comes from the program’s own data and may be missing or inaccurate.\n"
        L"• Microsoft Store apps and Windows components are not shown here.\n"
        L"• Leftovers in the registry or on disk are not touched, so nothing can break.\n"
        L"• The list refreshes when you return to the window.",
        L"Tous les programmes installés avec éditeur et taille.\n\n"
        L"• « Désinstaller » lance le programme de désinstallation du programme lui-même. Vous pouvez encore annuler à ce moment-là.\n"
        L"• La taille provient des indications du programme et peut manquer ou être inexacte.\n"
        L"• Les applications du Microsoft Store et les composants Windows ne sont pas affichés ici.\n"
        L"• Les restes dans le registre ou sur le disque ne sont pas touchés, pour que rien ne casse.\n"
        L"• La liste se met à jour quand vous revenez à la fenêtre.",
        L"Todos los programas instalados con editor y tamaño.\n\n"
        L"• «Desinstalar» inicia el desinstalador del propio programa. Allí aún puedes cancelar.\n"
        L"• El tamaño procede de los datos del programa y puede faltar o no ser exacto.\n"
        L"• Las aplicaciones de Microsoft Store y los componentes de Windows no se muestran aquí.\n"
        L"• No se tocan los restos en el registro ni en el disco, para que nada se estropee.\n"
        L"• La lista se actualiza al volver a la ventana.",
        L"Wszystkie zainstalowane programy z wydawcą i rozmiarem.\n\n"
        L"• „Odinstaluj” uruchamia własny deinstalator programu. Tam nadal możesz anulować.\n"
        L"• Rozmiar pochodzi z danych programu i może go brakować lub być niedokładny.\n"
        L"• Aplikacje ze Sklepu Microsoft i składniki Windows nie są tu pokazywane.\n"
        L"• Pozostałości w rejestrze lub na dysku nie są ruszane, żeby nic się nie zepsuło.\n"
        L"• Lista odświeża się, gdy wracasz do okna.",
        L"Todos os programas instalados com editor e tamanho.\n\n"
        L"• «Desinstalar» inicia o desinstalador do próprio programa. Lá você ainda pode cancelar.\n"
        L"• O tamanho vem dos dados do programa e pode estar ausente ou impreciso.\n"
        L"• Aplicativos da Microsoft Store e componentes do Windows não são mostrados aqui.\n"
        L"• Restos no registro ou no disco não são tocados, para que nada quebre.\n"
        L"• A lista é atualizada quando você volta para a janela.",
        L"インストール済みのすべてのプログラムを、発行元とサイズとともに表示します。\n\n"
        L"• 「アンインストール」を押すと、プログラム自身のアンインストーラーが起動します。そこでまだキャンセルできます。\n"
        L"• サイズはプログラムが登録した情報によるもので、表示されない場合や実際と異なる場合があります。\n"
        L"• Microsoft Store アプリと Windows のコンポーネントはここには表示されません。\n"
        L"• レジストリやディスクに残ったものには触れないので、何も壊れません。\n"
        L"• ウィンドウに戻ると、一覧が更新されます。",
        L"顯示所有已安裝的程式，包括發行者與大小。\n\n"
        L"• 「解除安裝」會啟動該程式自己的解除安裝程式。你仍可在那裡取消。\n"
        L"• 大小來自程式自行登記的資料，可能缺漏或不準確。\n"
        L"• 這裡不會顯示 Microsoft Store 應用程式與 Windows 元件。\n"
        L"• 不會動到登錄檔或磁碟上的殘留項目，以免弄壞任何東西。\n"
        L"• 回到視窗時，清單會自動更新。"
    },
};

# PgAdm Qt

Native PostgreSQL-beheeromgeving in C++ en Qt Widgets, met opgeslagen wachtwoorden via QtKeychain, een databaseboom, query-uitvoering en een doorlopend resultatenoverzicht. Beheerdialogen voor het wijzigen van objecten volgen daarna.

De technische opzet en gegevensstromen staan in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). De publieke headers beschrijven hun doel, voorwaarden, resultaat en interne samenwerking in het Engels. De projectafspraken vertalen Hans' Racket-stijl naar eenvoudige C++/Qt-code; Racket-specifieke syntax en packagevoorzieningen worden niet overgenomen.

## Gebruiken

1. Start `build\Release\pgadmin_qt.exe`, of Build/Run vanuit Qt Creator.
2. Kies **Verbinding > Nieuw profiel** en vul naam, host, poort en gebruiker in. Voor de gebruikelijke serververbinding: gebruiker **postgres**, onderhoudsdatabase **postgres**.
3. Kies de SSL-modus en de verbindingstime-out (standaard 10 seconden).
4. Selecteer het profiel en kies **Verbinden**, of dubbelklik op het profiel.
5. Als PostgreSQL een wachtwoord vereist, verschijnt een wachtwoordvenster. Vink **Wachtwoord onthouden** aan om het na succesvol verbinden via QtKeychain in de systeemkluis te bewaren. Bestaande libpq-voorzieningen zoals pgpass kunnen ook worden gebruikt.
6. De statusbalk toont de verbinding en het profiel wordt vet weergegeven. De tooltip bevat serverversie en backend-PID.
7. Open **Databases**, klik met rechts op bijvoorbeeld **lts_ontwikkel** en kies **Querytab openen**. De benodigde databaseverbinding wordt geopend en de tab krijgt die database als doel.
8. Gebruik **Verbinding verbreken / poging annuleren** om te sluiten of een verbindingspoging af te breken.

Profielen wijzigen of verwijderen kan als de verbinding is gesloten. Meerdere profielen kunnen tegelijk verbonden zijn, met een limiet van acht actieve verbindingen. Dubbelklikken opent voortaan een verbinding; wijzigen kan via het menu of de rechtermuisknop.

## Wachtwoord onthouden

Het profielvenster heeft een wachtwoordveld en de optie Wachtwoord onthouden. Daar kan een wachtwoord worden ingevoerd of gewijzigd. Een leeg veld laat een eerder opgeslagen wachtwoord intact. Uitvinken en opslaan verwijdert het opgeslagen wachtwoord. Een nieuw ingevoerd wachtwoord zonder onthouden blijft alleen in geheugen beschikbaar voor de eerstvolgende verbinding. Het verwijderen van een profiel verwijdert ook de bijbehorende kluisvermelding.

QtKeychain 0.17.0 gebruikt de systeemkluis; de applicatie gebruikt geen eigen Windows-wachtwoord-API. Plaintext-fallback is expliciet uitgeschakeld. Alleen de voorkeur voor onthouden staat in settings.ini. De sleutel is gekoppeld aan profiel, host, poort en gebruiker; het wachtwoord kan daarmee voor databases op dezelfde server worden hergebruikt. Een gewijzigde host of gebruiker krijgt een andere sleutel. Fouten bij kluisopslag worden gemeld en leiden niet tot opslag als gewone tekst.

## Databaseboom

Na verbinden verschijnt **server → Databases → database → schema → Tabellen / Views / Materialized views / Sequences → object → kolommen**. De boom laadt de inhoud bij het uitklappen. Template- en systeemschema's worden momenteel verborgen. Databases zonder CONNECT-recht of met uitgeschakelde verbindingen worden wel getoond, maar zijn niet te openen. Alleen schema's met USAGE-recht worden weergegeven.

Klik met rechts op een database en kies **Querytab openen**, of dubbelklik op de database. Deze actie werkt ook als de database al verbonden is en blijft onthouden wanneer het verbinden nog bezig is. **Verbinden** en **Querytab openen** zijn aparte acties. Iedere server/database-combinatie heeft een eigen queryverbinding; tabs met dezelfde combinatie delen een sessie. De onderhoudsdatabase van het profiel blijft ongewijzigd: een serververbinding op postgres kan naast queryverbindingen op lts_ontwikkel bestaan.

De keuzelijst boven de editor vermeldt server én database. Alleen een item selecteren in de boom verandert het doel van bestaande querytabs niet. Ctrl+N neemt de database van de huidige tab over. Als een verbinding wordt verbroken, blijft de gekozen database zichtbaar als niet verbonden en wordt uitvoeren uitgeschakeld; de tab schakelt niet stilzwijgend naar een andere database. Via de rechtermuisknop of **Verbinding > Objectboom vernieuwen** worden externe wijzigingen opgehaald.

De objectboom gebruikt aparte metadataverbindingen, zodat een lange gebruikersquery of open transactie de browser niet bezet houdt. Metadata wordt alleen gelezen, niet gewijzigd. Er zijn maximaal acht metadataverbindingen naast de maximaal acht queryverbindingen; ongebruikte metadataverbindingen worden bij behoefte vervangen. Profielen en vensterinstellingen laden lokaal; bij starten wordt niet automatisch verbonden en wordt de wachtwoordkluis niet geopend.

SSL-modi: prefer, require, verify-ca, verify-full en disable. Certificaatbestanden gebruiken nu de standaard libpq-instellingen. Certificaatkeuze via het profiel en SSH-tunnels zijn nog niet gebouwd. Een geslaagde TLS-verbinding met een eigen certificaatketen is nog niet afzonderlijk getest.

## Overige functies

Via de rechtermuisknop op een verbonden **server** of **database** zijn nu beschikbaar:

- **Database aanmaken...**: een lege UTF-8-database op basis van template0; eigenaar is de ingelogde gebruiker.
- **Databasebackup...**: een volledige database, inclusief data, naar een PostgreSQL custom-archief (`.backup`) of SQL-script (`.sql`). Dit gebruikt pg_dump rechtstreeks en valt niet onder de queryresultaatlimieten. Globale serverrollen worden niet door een databasebackup aangemaakt.
- **Database herstellen...**: herstel een archief met pg_restore of een SQL-bestand met psql naar de expliciet getoonde database. Maak bij voorkeur eerst een lege database aan. Archieven worden met één transactie en stoppen-bij-fouten hersteld. Eigenaren/privileges overslaan staat standaard aan; bestaande archiefobjecten eerst verwijderen staat standaard uit. Deze opties gelden alleen voor archieven. SQL-bestanden bepalen zelf hun SQL/psql-commando's; standaard pg_dump-scripts worden transactioneel uitgevoerd.
- **Database verwijderen...**: de exacte naam moet ter bevestiging worden ingetypt. postgres, template0/template1 en de onderhoudsdatabase van het profiel zijn beschermd. Andere clients worden niet geforceerd verbroken; PostgreSQL kan verwijderen weigeren wegens verbindingen of rechten.

Op serverniveau kies je voor backup, restore of verwijderen eerst de database uit de geladen lijst. Het taakvenster houdt zijn doel vast als je daarna elders in de boom klikt. Starten is de uitvoerbevestiging. De taken werken op de achtergrond met loguitvoer en **Annuleren**, terwijl de rest van het programma bruikbaar blijft. Tijdens de taak loopt **Verstreken tijd** mee in uren, minuten en seconden. Na slagen, falen of annuleren blijft de **Totale duur** staan, inclusief milliseconden. Bij backup telt ook het definitief opslaan van het bestand mee. De voortgangsbalk is onbepaald: de tools geven geen betrouwbaar totaalpercentage door. Een mislukt/geannuleerd backupverzoek vervangt geen bestaand bestand. CREATE/DROP kan bij laat annuleren al zijn voltooid; controleer dan de vernieuwde boom.

Restore en verwijderen sluiten de eigen verbindingen met de doeldatabase, inclusief metadata; open transacties worden teruggedraaid zoals het taakvenster vermeldt. Actieve eigen queries moeten eerst worden afgerond/geannuleerd. De doelboom wordt tijdelijk geblokkeerd voor nieuwe eigen verbindingen en na afloop ververst. Queryteksten blijven in hun tabs staan met de oorspronkelijke, eventueel nu verbroken databasecontext. Dit beheert databases; tabel-/schema-editors en rollenbeheer zijn nog vervolgwerk.

Klik met rechts op een database, schema, tabel, view, sequence of kolom en kies **Eigenschappen / definitie-SQL**. Het paneel **Objecteigenschappen** verschijnt onderaan en volgt daarna de selectie in de boom. Eigenschappen worden alleen opgehaald terwijl het paneel zichtbaar is, via dezelfde aparte metadataverbindingen als de boom. **Vernieuwen** haalt de geselecteerde eigenschappen opnieuw op. Het paneel kan ook via **Beeld** worden getoond of verborgen.

Bij tabellen, views/materialized views, sequences en kolommen haalt het tabblad **Definitie-SQL** op verzoek een schema-only script op via de meegeleverde PostgreSQL 18 `pg_dump`. Een kolom toont de definitie van het bovenliggende object. SQL kan worden gekopieerd of als bestand opgeslagen. Database- en schemanodes tonen voorlopig alleen eigenschappen.

Het definitiebestand bevat geen tabeldata, eigenaar- of privilegecommando's. Het is een origineel pg_dump-script voor **psql** en kan psql-directieven bevatten die de query-editor niet ondersteunt. De definitie van één object garandeert niet dat alle afhankelijke schema's/types/objecten zijn meegenomen. Dit is geen volledige backup. Ontbrekende rechten, een verdwenen object, ontbrekende tools en toolfouten worden getoond; een geblokkeerde lock wordt na twee seconden opgegeven en de gehele taak na dertig seconden. Een andere selectie of het sluiten van het paneel stopt een overbodige dump. Er wordt geen definitie-SQL automatisch uitgevoerd.

- Meerdere SQL-tabbladen met Ctrl+N en Ctrl+W.
- UTF-8 SQL-bestanden openen en opslaan met Ctrl+O en Ctrl+S.
- SQL-syntaxkleuring voor sleutelwoorden, tekst, geciteerde namen, getallen en commentaar, inclusief genest blokcommentaar en dollar-quoted PostgreSQL-bodies.
- Markering en controle op niet-opgeslagen wijzigingen.
- Tabbladen Resultaten, Meldingen en Uitvoeringsplan.
- Verplaatsbare objectbrowser, aanpasbare editor/resultaatverdeling en opgeslagen vensterindeling.

## Queries en resultaten

Het rechterpaneel **Bewaarde queries** bevat lokaal opgeslagen scripts. Kies **Huidige opslaan...** (of Ctrl+Alt+S) en geef een naam. Zoek in de lijst en dubbelklik of druk Enter om een query te openen; de huidige databasekeuze wordt overgenomen. Een al geopende query krijgt de focus, zodat onopgeslagen bewerkingen behouden blijven. Ctrl+S bewaart volgende wijzigingen. Hernoemen en verwijderen kan in hetzelfde paneel. Verwijderen houdt de tekst in open tabs beschikbaar als niet-opgeslagen query. Via **Beeld** kan het paneel worden verborgen of teruggehaald.

Kies boven de querylijst **Alle categorieën**, **Zonder categorie** of een eigen categorie.
Selecteer een query en klik **Categorie...** om een bestaande categorie te kiezen
of een nieuwe naam te typen. Maak het veld leeg om de query weer zonder categorie
te bewaren. Zoeken werkt binnen de gekozen categorie. Nieuwe queries krijgen bij
opslaan de geselecteerde categorie; bestaande queries behouden hun categorie.
Categorieën zonder queries verdwijnen uit de keuzelijst. Querynamen blijven uniek
binnen de hele bibliotheek.

De scripts staan als gewone UTF-8 `.sql`-bestanden in de map `queries` naast `settings.ini` (standaard de applicatieconfiguratiemap). De bestandsnamen coderen de querynamen; bij starten worden de namen en categorieën gelezen. SQL wordt pas geladen bij openen. De categorieën staan apart in `queries/categories.ini`; neem dat bestand mee bij het kopiëren van je querybibliotheek. Categoriseren verandert de SQL-bestanden en hun kolombreedtes niet.

Sleep de scheidingslijnen in de bovenste kolomkop om de breedte van resultaatkolommen te wijzigen. Dubbelklik op een scheidingslijn voor passend maken, of klik met rechts op de kolomkop voor een exacte breedte, alle kolommen passend maken of de standaardbreedte herstellen. Breedtes gelden per kolompositie in het doorlopende overzicht. Elke bewaarde query en elk SQL-bestand onthoudt automatisch zijn eigen breedtes, ook na herstarten. Bij hernoemen in het paneel gaat deze indeling mee. Nieuwe, nog niet opgeslagen queries gebruiken de algemene breedtes. De indeling staat lokaal in de instellingen, niet in de SQL zelf.

Kies boven de editor een verbonden server/database en druk F5. Als tekst is geselecteerd, wordt alleen die selectie uitgevoerd; anders het hele script. De opdrachten worden afzonderlijk en in volgorde uitgevoerd. Bij de eerste fout stopt het script. Zonder expliciete BEGIN/COMMIT is elke geslaagde opdracht afzonderlijk gecommit. Open transacties en een benodigde ROLLBACK worden bij afronding getoond. Tabs met dezelfde server/database delen op dit moment de sessie en transactie; per verbinding kan één uitvoering tegelijk lopen. De gekozen verbinding staat expliciet boven de editor.

Onder de editor staat één native QTableView met een eigen model. Resultaatnamen, kolomkoppen, rijen en voltooiingsmeldingen lopen door in hetzelfde overzicht, ook als opdrachten verschillende kolommen teruggeven. Er worden geen aparte resultaattabs per opdracht gemaakt.

- **Tussenresultaten tonen**: standaard aan. Uitgevinkt blijft alleen het laatste resultaat zichtbaar. De instelling geldt bij de volgende uitvoering en wordt bewaard.
- **Kopiëren**: zet het hele weergegeven overzicht op het klembord als tekst met tabgescheiden tabelwaarden en als HTML-tabel voor plakken in Excel, inclusief headings, volledige opgeslagen celwaarden en statusregels.
- **CSV opslaan...**: UTF-8 met BOM, komma als scheidingsteken en aangehaalde velden met correct verdubbelde aanhalingstekens. Eén resultaat geeft kolomkoppen en gegevens; meerdere resultaten geven afzonderlijke benoemde blokken in hetzelfde bestand.
- **Excel opslaan...**: één `.xlsx`-werkblad met de resultaatblokken en de ingestelde kolombreedtes. Waarden worden als tekst geschreven, zodat grote getallen, voorloopnullen en tekst die met `=` begint intact blijven. Excel ondersteunt maximaal 32.767 tekens per cel; bij een langere waarde stopt de export met een melding en kan CSV worden gebruikt. Een bestaand bestand blijft dan intact.
- Exports schrijven op de achtergrond en vervangen het doelbestand pas na succesvol schrijven. Ze gebruiken de lokaal opgeslagen resultaten; ingekorte waarden worden gemarkeerd en bij export wordt vooraf om bevestiging gevraagd. NULL wordt weergegeven als `NULL` en bytea als `\\x...`, net als bij kopiëren.
- Een naam voor het volgende resultaat kan met `-- @resultaat: Klantenoverzicht` vóór de opdracht.
- EXPLAIN-uitvoer blijft onderdeel van het overzicht. De aparte Uitvoeringsplan-tab bevat daarnaast het laatst beschikbare plan.
- Servermeldingen en fouten staan ook onder Meldingen. Query annuleren onderbreekt de lopende opdracht en voert de volgende opdrachten niet uit.

De SQL-splitter herkent gewone en E-strings, dubbele quotes, dollar-quoted bodies, regelcommentaar en genest blokcommentaar. Het is geen volledige PostgreSQL-grammaticaparser; onder andere psql-commando's en COPY-gegevensstromen worden niet ondersteund. Een COPY-gegevensstroom sluit de verbinding met een melding. Gebruik standaard `standard_conforming_strings=on`; de client stopt wanneer dit wordt uitgezet. PostgreSQL-specifieke compound SQL-bodies zonder dollar-quotes zijn nog geen ondersteund scriptscenario.

## Grote celwaarden en limieten

Een tabelcel toont maximaal ongeveer 200 tekens, of een korte binaire preview. Eén klik toont details rechts; dubbelklikken opent een groter dialoogvenster. Tekst loopt automatisch om binnen de beschikbare breedte, ook bij lange woorden. De viewer toont tekst in pagina's van 50.000 tekens en binaire data als hex in pagina's van 4 KiB. Hexregels blijven ongewijzigd uitgelijnd. De beschikbare volledige waarde kan worden gekopieerd of bewaard; bij bytea bewaart de bestandsactie de oorspronkelijke binaire bytes.

Een korte preview verandert de opgeslagen waarde niet. Voor zeer grote resultaten gelden wel afzonderlijke opslaglimieten: 20.000 rijen per opdracht, 50.000 rijen per uitvoering en een opslagbudget van circa 32 MiB inclusief een conservatieve bijdrage per cel. Een cel bewaart maximaal 2 MiB aan UTF-8-invoer; bij bytea is dit de gecodeerde invoer, waardoor minder ruwe bytes overblijven. Het resterende budget kan deze limiet verder verlagen. Ingekorte uitvoer wordt expliciet gemarkeerd, ook bij kopiëren. Bewaren van een ingekorte cel vraagt of alleen het beschikbare deel moet worden bewaard.

De client blijft de overige resultaten verwerken en telt de werkelijk ontvangen rijen. De limiet herschrijft de SQL niet en beperkt serverwerk of netwerkverkeer dus niet automatisch. libpq single-row mode voorkomt verzamelen van het volledige resultaat; één extreem grote rij kan wel tijdelijk meer geheugen gebruiken dan het opslagbudget. De overdracht naar de interface gebruikt één batch tegelijk met ontvangstbevestiging.

OID-waarden worden als waarden getoond. Een OID is niet automatisch een large-objectverwijzing. Het op verzoek inlezen/exporteren van PostgreSQL large objects via OID is nog een aparte vervolgstap; de huidige detailviewer toont de ontvangen tekst- en bytea-waarden.

## Afhankelijkheden

- Qt 6.11.1, MSVC 2022 x64, CMake en Ninja op deze machine.
- Projectlokale PostgreSQL 18.6-5 SDK en runtime uit het officiële EDB-binaire pakket.
- Qt Network voor asynchroon opzoeken van hostnamen.
- QXlsx 1.5.1.1 voor Excel-export, via CMake opgehaald met vaste SHA-256 en statisch meegebouwd; de MIT-licentie wordt naast de executable geplaatst. Qt Concurrent voert bestandsexport op de achtergrond uit.
- QtKeychain 0.17.0, automatisch opgehaald door CMake FetchContent met vastgelegde SHA-256, statisch meegebouwd. De eerste configuratie heeft hiervoor internet nodig; daarna wordt de buildcache gebruikt. De bijbehorende licentie wordt naast de executable geplaatst.

De PostgreSQL 17-installatie wordt niet gebruikt door de nieuwe build. Download de projectlokale bestanden met:

```powershell
py -3 tools\fetch-postgresql.py
```

Python is alleen een hulpmiddel voor het ophalen van dependencies; de applicatie start geen Python-proces. De download gebruikt delen van het officiële ZIP-archief en haalt geen pgAdmin-installatie op. Bestanden komen onder `third_party\postgresql18`; versienummer, bron-URL en bestandshashes staan in `manifest.json`. Het downloaden installeert geen service en wijzigt geen systeem-PATH of databases.

De map bevat ook server- en commandlinetools voor geïsoleerde integratietests. De applicatie gebruikt alleen de client-DLL's. De downloadmap en buildbestanden zijn uitgesloten in `.gitignore`.

QPSQL is een bruikbaar alternatief voor Qt SQL-toegang, en is aanwezig in de gebruikte Qt-installatie. De applicatie gebruikt momenteel rechtstreeks libpq, met nonblocking verbindingen in een workerthread. Ook QPSQL vereist libpq en bijbehorende runtime-DLL's.

## Bouwen

Open `CMakeLists.txt` in Qt Creator met de Qt 6.11.1 MSVC 2022 64-bit-kit. Voer na het ophalen van dependencies opnieuw CMake uit. De PostgreSQL 18-root staat in de CMake-instelling `PGADM_POSTGRESQL_ROOT`; standaard is dat de projectlokale map.

Voor een standalone Release-build:

```powershell
.\build-release.cmd
```

Het script laadt de lokale Visual Studio-omgeving, bouwt met CMake/Ninja en draait windeployqt. CMake kopieert libpq en de bijbehorende DLL's naast de executable, ook bij builds vanuit Qt Creator. De PostgreSQL-tools `pg_dump.exe`, `pg_restore.exe`, `psql.exe`, `createdb.exe` en `dropdb.exe` en hun runtimebestanden, waaronder `liblz4.dll` en `libzstd.dll`, worden meegekopieerd. Houd deze bestanden bij de executable. Een volledige distributie op een schone Windows-machine is nog niet gecontroleerd.

De vensteropbouw staat in `pgadmmain.cpp` en `querytab.cpp`. Het oude `.ui`-bestand wordt niet voor de schermindeling gebruikt. `pgsession.cpp` bevat de verbinding en worker; de PGconn verlaat de workerthread niet.

## Instellingen en starten

Instellingen staan onder Qt AppConfigLocation voor Hans/PgAdmQt in `settings.ini`. Het profiel bewaart geen wachtwoord. Bij starten worden geen databaseverbindingen geopend en geen queries opnieuw uitgevoerd.

Gebruik `--settings-file <pad>` voor een apart instellingenbestand. De bestaande `--startup-check --startup-report <pad>`-opties meten van het begin van main() tot de eerste eventloop-callback na show() en sluiten automatisch af. Maak de rapportmap vooraf aan. Dit omvat geen loader-tijd vóór main() en is geen garantie dat alle pixels op dat moment zijn getekend.

## Rollen en databaserechten

Verbind met de server en kies via de rechtermuisknop **Rollen en databaserechten...**.
Rollen gelden voor de hele server. Je kunt rollen aanmaken en bestaande rollen
bewerken: LOGIN, wachtwoord, vervaldatum, verbindingslimiet, beheerdersattributen,
commentaar en lidmaatschappen. Bestaande rolnamen worden hier niet hernoemd.
Wijzigingen verschijnen eerst als SQL en worden pas met **Toepassen** uitgevoerd.
Nieuwe rollen blijven standaard NOLOGIN, zonder beheerdersrechten.
Wachtwoorden verschijnen niet in het SQL-voorbeeld en worden als SCRAM-verifier
naar PostgreSQL verzonden. Het verbindingsprofiel wordt niet automatisch aangepast
als je het wachtwoord van diezelfde gebruiker wijzigt.

Het tabblad **Databaserechten** toont directe CONNECT/CREATE/TEMPORARY-grants,
GRANT OPTION en effectieve rechten. Rechten via PUBLIC of een andere rol kunnen
blijven bestaan na het intrekken van een directe grant. Database-eigendom wijzigen
is een aparte keuze. Tabel- en schemarechten vallen buiten dit venster.
Pas wijzigingen op een tabblad toe voordat je het andere tabblad opent.
PostgreSQL controleert of je beheergebruiker de gevraagde bewerking mag uitvoeren.

Een SQL-backup van een database bevat vaak eigenaarsnamen en GRANT-opdrachten,
maar maakt de serverrollen zelf niet aan. Ontbreekt bij herstel bijvoorbeeld
`sparxea`, open dan **Rollen beheren...** vanuit het herstelvenster, maak die rol
aan en start het herstel daarna opnieuw vanuit het databasecontextmenu. Als de rol
alleen eigenaar moet zijn, kan LOGIN uit blijven. Voor werkelijk aanmelden moet
je LOGIN en een passend wachtwoord instellen. De optie om eigenaars/rechten over
te slaan geldt uitsluitend voor PostgreSQL-archieven, niet voor SQL-bestanden.

## Testen

Querycategorieën zijn op 9 november 2026 gericht gecontroleerd met
`tst_browser savedQueriesAndWidths`: geslaagd, inclusief bestaande queries zonder
categorie, toewijzen via de interface, categorie- en zoekfilters, hernoemen,
behoud na herstarten, kolombreedtes en ongewijzigde SQL-tekst. De test gebruikt
een tijdelijke bibliotheek. Rapport: `build/Release/query-categories-check.txt`.

Tijdweergave gecontroleerd op 9 november 2026: alle 36 controles geslaagd,
zonder fouten of overgeslagen tests. Het bestaande backup-/restorevensterscenario
controleert nu ook de beginwaarde, lopende tijdweergave en totale duur na afloop.
Rapporten: `build/Release/integration-ad1189b46c0b4a719243ec36d2fc94f9/`.
De tijdelijke PostgreSQL 18-server is gestopt.

Rollenbeheer gecontroleerd op 9 november 2026: 36 geslaagde controles (11
verbinding, 11 query/model/view/export, 14 kluis/browser/taken), geen fouten of
overgeslagen tests. Nieuwe controles dekken quoten en alleen gewijzigde SQL,
de echte contextmenuactie, een SQL-restore met ontbrekende eigenaarsrol gevolgd
door succesvol herstel na rolcreatie, NOLOGIN als standaard, rollback van
gedeeltelijke rolcreatie bij een verdwenen lidmaatschapsdoel, LOGIN met een lokaal
voorbereid SCRAM-wachtwoord, weigeren van een verkeerd wachtwoord, lidmaatschap,
directe databasegrants met GRANT OPTION en intrekken daarvan naast PUBLIC-rechten.
Ook het terugdraaien van formulierinvoer en weigeren van rolcreatie door een
beperkte gebruiker zijn getest. Rapporten:
`build/Release/integration-f16ecd2c789c4e49a0df9a0636a39987/`.
De tijdelijke PostgreSQL 18-server is gestopt; bestaande databases zijn niet gebruikt.

Backup/restore en create/drop: 34 geslaagde controles (11 verbinding, 11 query/model/view/export, 12 kluis/browser/taken), geen fouten of overgeslagen tests. De roundtrip controleert echte backups naar andere databases, ook 25.001 rijen buiten de queryweergavelimiet, grote getallen, Unicode/regeleinden, bytea, sleutels/indexen, views en sequencestatus. Falen/annuleren behoudt een bestaand backupbestand; een fout SQL-herstel rolt terug. De echte contextmenu's controleren create buiten een open querytransactie, een vastgezette restoretarget, sluiten van eigen sessies, boomverversing en naambevestiging bij drop. Rapporten: `build/Release/integration-6cc635213bfb4fd78b4f315dc8cd7a99/`. De tijdelijke PostgreSQL-server is gestopt. De lege-bestandsnaamcontrole is na afloop nog afzonderlijk offline getest.

Gecontroleerd op 8 november 2026 na documentatie/refactor en objectinspectie: 31 geslaagde controles (11 verbinding, 11 query/model/view/export, 9 kluis/browser), geen fouten of overgeslagen tests. De nieuwe tests controleren profielannulering zonder wijzigingen, gescheiden wachtwoordvelden, de browseractie voor eigenschappen, echte cataloguswaarden en pg_dump-definities met quotes/wildcards, selectievolgorde, beperkte rechten, SCRAM-login van pg_dump, verdwenen objecten en een gelockte tabel zonder geblokkeerde interface. Rapporten: `build/Release/integration-688abdf9879244268313d89e651692fa/`. De tijdelijke server is gestopt.

Gecontroleerd op 8 november 2026: 29 geslaagde controles (11 verbinding, 11 query/model/view/export, 7 kluis/browser), geen fouten of overgeslagen tests. Inclusief CSV-quoting en Unicode, XLSX-teruglezen zonder verlies van voorloopnullen/grote getallen/literale SQL-tekst, behoud van een bestaand bestand bij een te lange Excel-cel, daadwerkelijke exportknoppen en tekst/HTML op het klembord. Rapporten: `build/Release/integration-fb371492613e48818fecf50aaaf85e06/`. De tijdelijke PostgreSQL-server is gestopt.

```powershell
.\build-release.cmd -DPGADM_BUILD_TESTS=ON
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run-integration.ps1
```

De ExecutionPolicy-optie geldt alleen voor dat testproces. Het script maakt een aparte PostgreSQL 18-cluster met een vrije loopbackpoort, maakt een tijdelijke testrol aan en stopt de server in een finally-blok. Bestaande databases en services worden niet aangepast. Testdata en het rapport blijven in een unieke `build\Release\integration-*`-map staan.

De verbindingstests controleren verbinding weigeren, timeout met een responsieve eventloop, annulering, verbinden/herverbinden, ontbrekend en verkeerd wachtwoord, SCRAM-authenticatie, ontbrekende database, hostname-resolutie, vereiste SSL op een server zonder TLS en onverwacht verbreken. De querytests controleren scriptopsplitsing (ook het aangeleverde voorbeeld met elf opdrachten), native tabelweergave en clipboardtekst, grote celpreviews, meerdere queryresultaten en EXPLAIN-plannen, servermeldingen, fouten en rollback, annulering met hergebruik van de verbinding en opslaglimieten. Het voorbeeldscript zelf wordt niet op een database uitgevoerd. Zonder PGADM_TEST_PORT worden echte databasetests overgeslagen; het script stelt die variabele zelf in.

De browsertests controleren QtKeychain opslaan/lezen/overschrijven/verwijderen met uitsluitend unieke testvermeldingen, zonder eigen gebruikerswachtwoorden te lezen. Een hoofdvenster met een apart testinstellingenbestand verbindt met een opgeslagen SCRAM-wachtwoord, laadt de boom tot kolommen en opent via dubbelklik een query op een andere database. SELECT current_database() controleert het daadwerkelijke doel. Een schema met een apostrof in de naam test correcte SQL-quoting. De test controleert ook dat de kladconfiguratie geen wachtwoord bevat en dat metadataverzoeken doorgaan terwijl een gebruikersquery loopt. De testvermeldingen worden opgeruimd.

Gecontroleerd op 5 november 2026: Release-build met PostgreSQL 18.6, 11 geslaagde verbindingscontroles, 8 geslaagde query/model/view-controles en 5 geslaagde kluis/browsercontroles (inclusief initialisatie en opruimen), geen fouten of overgeslagen tests. De rapporten staan in `build/Release/integration-a62c2e0e6cf14cbe84706f5c1ea0b61b/`; de uiteindelijke opgeslagen-wachtwoordroute, databasekeuze en serverdisconnect staan in `browser-final.txt`. De tijdelijke server is gestopt. Een warme opstartcontrole bereikte de eerste eventloop-callback na 129 ms vanaf main(), exclusief loader-tijd. GUI-tests draaien met de meegeleverde offscreen-platformplugin; een ontbrekende testplugin wordt vóór starten gemeld. De interactieve wachtwoorddialoog is nog niet met een echte gebruikersverbinding doorlopen.

Gecontroleerd op 6 november 2026: 25 geslaagde controles (11 verbinding, 8 query/model/view, 6 kluis/browser), geen fouten of overgeslagen tests. Het extra scenario start als postgres op postgres en opent via het echte contextmenu een querytab op een tijdelijke database met de naam lts_ontwikkel. current_user en current_database() bevestigen het doel. Ook een tweede tab op dezelfde database, Ctrl+N, het behoud van de onderhoudsdatabase en geen terugval naar postgres na verbreken zijn gecontroleerd. Rapporten: `build/Release/integration-8748080f48884ca482262eee74c7dcad/`. Dit controleert de programmaroute op een aparte testserver, niet de toegangsinstellingen van een bestaande gebruikersserver.

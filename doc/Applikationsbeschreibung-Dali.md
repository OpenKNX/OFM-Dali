# **DALI-Gateway**

<!-- DOC HelpContext="Dokumentation" -->

<!-- DOCCONTENT
Eine vollständige Applikationsbeschreibung ist unter folgendem Link verfügbar: https://github.com/OpenKNX/OFM-Dali/blob/v1dev/doc/Applikationsbeschreibung-Dali.md
DOCCONTENT -->

Das Modul steuert DALI-Treiber über KNX: Einzeladressen, Gruppen und Broadcast, mit Schalten, Dimmen, Farbe, HCL, Sperre und Szenen.
<!-- DOCEND -->

# Nach Geräten suchen


<!-- DOC HelpContext="scan-without" -->
### Nur ohne Adresse

Bei dieser Option, werden nur EVGs gesucht, denen noch keine Kurzadresse (0-64) zugewiesen wurde.  

<!-- DOC HelpContext="scan-generate" -->
### Adr. neu generieren

Bei dieser Option generiert jedes EVG vor der Suche eine neue Langadresse.  
Die Langadresse wird auch Zufallsadresse genannt und wird nur für die Suche verwendet.  
Sie muss nicht permanent sein und kann sich somit auch nach einem Spannungswegfall ändern.  

<!-- DOC HelpContext="scan-delete" -->
### Kurzadressen löschen

Ist diese Option aktiviert, werden von **allen** EVGs die Kurzadresse gelöscht.  
Sie sind dann nicht mehr einzeln ansprechbar.  

<!-- DOC HelpContext="scan-auto" -->
### Kurzadressen setzen

Ist diese Option aktiviert, werden alle EVGs ohne eine Kurzadresse, eine freie Kurzadresse zugewiesen.  
Diese Funktion füllt lücken von unten nach oben auf.  
# RGBW

Für DT8-Treiber mit vier Kanälen (Rot, Grün, Blau, Weiß) gibt es bei Einzeladressen und Gruppen den Farbtyp **RGBW**. Die Farbe kommt über ein 6-Byte-Objekt (DPT 251.600), die Helligkeit wie gewohnt über *Schalten*, *Dimmen absolut* und *Dimmen relativ*. Farbe und Helligkeit, die kurz nacheinander eintreffen, gehen als **ein** DALI-Befehl an den Treiber und blenden gemeinsam in einem Fade (einem weichen Übergang, dessen Dauer die Fade-Zeit des Treibers bestimmt).

Voraussetzung ist, dass der Treiber die DT8-Funktion „automatische Aktivierung“ beherrscht und sie eingeschaltet ist (ab Werk üblich). Dabei merkt sich der Treiber eine gesendete Farbe zunächst nur vor und übernimmt sie mit dem nächsten Helligkeitsbefehl, sodass Farbe und Helligkeit gemeinsam überblenden. Getestet wurde mit dem DA4-M im RGBW-Modus.

## **Treiber einrichten**

Beim Treiber im Reiter *DALI Einstellungen* die Gruppen anhaken und übertragen. Für RGBW empfiehlt sich *Power On Level* und *Failure On Level* = **Inaktiv**: Dann kommt der Treiber nach einem Ausfall seiner Versorgung mit dem letzten Pegel und der letzten Farbe zurück und bleibt bei einem Ausfall des DALI-Busses unverändert. Mit dem Werkswert (100 %) leuchtet er danach voll weiß.

## **Einzeladresse und Gruppe**

### **Farbe ansteuern per**

Mit **RGBW** werden die Objekte *Farbe RGBW* und *Farbe RGBW Status* eingeblendet. Bei Einzeladressen muss der *Gerätetyp* „DT8 – Farbe“ sein, bei Gruppen „Farbig“.

### **Helligkeit bei RGBW**

Legt fest, woher die Helligkeit kommt.

**Über Objekt „Dimmen absolut“ (Farbe = nur Mischung):** Die Farbe bestimmt nur das Verhältnis der Kanäle zueinander, die Helligkeit kommt über *Schalten*, *Dimmen absolut* und *Dimmen relativ*. Richtig für Visualisierungen mit eigener Helligkeitsadresse.

**Aus dem RGBW-Wert (größter Kanal = Helligkeit):** Der größte Kanal des RGBW-Werts ist die Helligkeit, eine Farbe größer 0 schaltet ein, eine Farbe aus lauter Nullen schaltet aus. Für Visualisierungen, die nur ein Farbobjekt senden. *Dimmen absolut* bleibt nutzbar, der Status von *Farbe RGBW* folgt dann der Helligkeit.

### **Bei Entsperren vorherigen Zustand wiederherstellen**

Nur bei RGBW und eingeblendetem Sperrobjekt.

**Nein:** Befehle während der Sperre werden verworfen, beim Entsperren gilt *Verhalten bei Entsperren*.

**Ja:** *Schalten*, *Dimmen absolut* und *Farbe RGBW* während der Sperre werden gespeichert. Beim Entsperren geht der gespeicherte Zustand (Ein/Aus, Helligkeit, Farbe) als ein Befehl an den Treiber, *Verhalten bei Entsperren* gilt dann nicht.

### **Weitere Parameter bei RGBW**

* *Einschaltwert Tag / Nacht*: Helligkeit beim Einschalten, wenn keine HCL-Helligkeit aktiv ist. 0 = letzter Dimmwert.
* *Verhalten bei Sperre / Entsperren*: Bei RGBW wirkt die Sperre nur auf die Helligkeit, die Farbe bleibt im Treiber. *Anschalten* = Einschaltwert, *Ausschalten* = aus, *Fester Wert* in Prozent; Gruppen verwenden ihre eigenen Sperr-Parameter.
* *HCL aktivieren*: Die HCL-Helligkeit wird bei RGBW verwendet, eine aktivierte Farbtemperatur wird übersprungen.
* *Farbe übertragen per*: ohne Bedeutung bei RGBW.

## **Kommunikationsobjekte**

| Objekt | DPT | Richtung | Beschreibung |
|---|---|---|---|
| Farbe RGBW | 251.600 | Eingang | Rot, Grün, Blau, Weiß (je 0–100 %) mit Gültigkeitsbits. Bei Einzeladressen bleiben Kanäle ohne Gültigkeitsbit unverändert, Gruppen bekommen immer die vollständige Farbe (Kanäle ohne Gültigkeitsbit = 0). |
| Farbe RGBW Status | 251.600 | Ausgang | zuletzt gesendete Farbe |
| Dali: Broadcast – Farbe RGBW | 251.600 | Eingang | Farbe an alle RGBW-Treiber, Helligkeit (größter Kanal) an alle Treiber; alles 0 = aus. Die Kanäle und Gruppen behalten ihren gespeicherten Zustand, es wird kein Status gesendet. |
| Dali: Broadcast – Zurück | 1.015 | Eingang | 1 = jede RGBW-Gruppe und jeder RGBW-Kanal, der in keiner RGBW-Gruppe liegt, bekommt seinen gespeicherten Zustand wieder (z. B. nach einem Blinklicht über *Broadcast – Farbe RGBW*). Gesperrte Kanäle und Gruppen werden übersprungen. |

## **Verhalten**

* **Einschalten** (*Schalten* Ein, *Dimmen absolut* > 0): gespeicherte Farbe und Helligkeit in einem Fade.
* **Ausschalten** (*Schalten* Aus, *Dimmen absolut* 0): Helligkeit 0, die Farbe bleibt in Gateway und Treiber gespeichert.
* **Farbe an einen ausgeschalteten Kanal:** Die Farbe wird im Treiber gesetzt, die Lampe bleibt aus. Beim nächsten Einschalten kommt sie in dieser Farbe.
* **Farbe allein bei brennender Lampe:** Einzeladresse: nur die Farbe, die Helligkeit bleibt. Gruppe: Farbe mit der gespeicherten Gruppenhelligkeit, damit alle Treiber der Gruppe denselben Zustand haben.
* **Helligkeit allein bei brennender Lampe:** Einzeladresse: nur die Helligkeit, die Farbe bleibt. Gruppe: Helligkeit mit der gespeicherten Gruppenfarbe.
* **Farbe und Helligkeit innerhalb von 80 ms:** ein Befehl, ein Fade. Das Gateway wartet nach dem ersten Telegramm diese Zeit (Sammelfenster, einstellbar mit `aawin`) auf das zweite. Zwei Befehle mit größerem Abstand können sich gegenseitig den Fade abbrechen, weil der Treiber nur einen Fade gleichzeitig ausführt.
* **Dimmen relativ:** wie bei den anderen Kanaltypen; nach dem Ende wird die Farbe noch einmal gesendet und die erreichte Helligkeit abgefragt.
* **Broadcast Schalten / Dimmen:** nur Helligkeit an alle Treiber, jede RGBW-Leuchte in der Farbe aus ihrem Treiber.
* **HCL:** Beim Einschalten und, wenn *HCL anwenden* = „Auch wenn eingeschaltet ist“, bei brennender Lampe kommt die HCL-Helligkeit zusammen mit der Farbe. Während einer Sperre sendet die HCL nichts an die Lampe.
* **Szenen:** unverändert, der Treiber fährt seine gespeicherten Werte an. Die Statusobjekte werden danach nicht nachgeführt.
* **Treppenhauslicht:** ohne RGBW-Behandlung, nur die Helligkeit; die Farbe kommt aus dem Treiber.
* **Nach einem Neustart des Gateways** ist die Farbe bis zum ersten Farbtelegramm unbekannt, beim Einschalten geht bis dahin nur die Helligkeit.

## **Welche Helligkeit gilt?**

1. Die zuletzt zugeschickte Helligkeit (*Dimmen absolut*, *Dimmen relativ*, nach einem Neustart die aus dem Treiber gelesene, im HCL-Automatikmodus die HCL-Nachführung). Sie gilt für *Broadcast – Zurück*, für das Entsperren mit „wiederherstellen“ und für *Dimmen absolut* während der Sperre.
2. Die HCL-Helligkeit, wenn HCL aktiv ist und im Automatikmodus steht: beim Einschalten, bei *Anschalten* durch die Sperre und wenn keine letzte Helligkeit bekannt ist.
3. Der *Einschaltwert Tag / Nacht*; steht er auf 0, die letzte Helligkeit.

*Min* und *Max* gelten als Grenzen für alle Werte.

## **Konsolenbefehle**

| Befehl | Funktion |
|---|---|
| `getCol yy [sel]` | fragt Farbmerkmale, Farbstatus und Farbwerte des Treibers `yy` ab, ändert nichts |
| `aastat` | zeigt für alle RGBW-Kanäle, ob die automatische Aktivierung im Treiber eingeschaltet ist |
| `aaon [yy]` | schaltet die automatische Aktivierung dauerhaft im Treiber ein (ohne `yy` bei allen RGBW-Kanälen) |
| `aawin nnn` | Sammelfenster in ms (0–1000, Standard 80, 0 = sofort senden), gilt bis zum Neustart |

Kommen Farbe und Helligkeit nicht in einem Fade, zuerst mit `aastat` prüfen und bei *OFF* mit `aaon` einschalten.

# **Infrarot**

<!-- DOC HelpContext="Dokumentation" -->

<!-- DOCCONTENT
Eine vollständige Applikationsbeschreibung ist unter folgendem Link verfügbar: https://github.com/openknx/OFM-Infrared/blob/v1/doc/Applikationsbeschreibung-Infrarot.md
DOCCONTENT -->

Das Modul ermöglicht das Senden und Empfangen von IR-Codes.
<!-- DOCEND -->

## **Allgemein**

Auf dieser Seite sieht man die in der Applikation verwendete Modulversion und die Anzahl der IR-Codes, die der Benutzer verwenden möchte.

### **Verfügbare Kanäle**

Um die Applikation übersichtlicher zu gestalten, kann hier ausgewählt werden, wie viele Kanäle in der Applikation verfügbar und editierbar sind. Die Maximalanzahl der Kanäle hängt von der Firmware des Gerätes ab, dass dieses Modul verwendet.

Die ETS ist auch schneller in der Anzeige, wenn sie weniger (leere) Kanäle darstellen muss. Insofern macht es Sinn, nur so viele Kanäle anzuzeigen, wie man wirklich braucht.

## **Tasen**

<!-- DOC -->
### **Modus**

Hier wählst du ob einen IR-Code einer Fernbediunung empfangen möchtest um etwas zu schalten oder aber ob du einen IR-Code senden möchtest.

<!-- DOC HelpContext="Code" -->
### **IR-Code**

Der IR-Code sind die Rohdaten zum Senden oder Emfpangen. Der zuletzt empfange IR-Code kann über den Button "Auslesen" direkt vom Gerät gelesen werden. Über "Senden" kann dieser Code auch testweise gesendet werden.

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
// GTIN company-prefix -> vendor lookup for common DALI gear manufacturers.
// Checked against the GTIN body (indicator digit stripped).
var VENDOR_BY_GTIN_PREFIX = [
    // eldoLED
    { prefix: "8717748", name: "eldoLED (Acuity Brands)" },
    { prefix: "6970602", name: "MiBoxer / Futlight" },
    { prefix: "4038816", name: "MiBoxer (Synergy 21 rebrand)" },
    { prefix: "4251530", name: "MiBoxer (MLZ rebrand)" },
    { prefix: "6975698", name: "Guangzhou Skydance" },
    { prefix: "6975617", name: "Guangzhou Skydance" },
    { prefix: "9009712", name: "Lunatone Industrielle" },
    { prefix: "9008272", name: "Lunatone Industrielle" },
    { prefix: "9006319", name: "Tridonic GmbH & Co KG" },
    { prefix: "900827", name: "Tridonic GmbH & Co KG" },
    { prefix: "9005080", name: "Zumtobel Lighting GmbH" },
    { prefix: "4008321", name: "OSRAM / LEDVANCE" },
    { prefix: "4052899", name: "OSRAM / LEDVANCE" },
    { prefix: "8711500", name: "Signify / Philips Lighting" },
    { prefix: "8718696", name: "Signify / Philips Lighting" },
    { prefix: "6418677", name: "Helvar Oy Ab" }
];

// Fallback: GS1 country prefix ranges (first 3 digits of the GTIN body), not exhaustive.
var GTIN_COUNTRY_PREFIXES = [
    { min: 0, max: 139, name: "USA/Canada" },
    { min: 300, max: 379, name: "France" },
    { min: 400, max: 440, name: "Germany" },
    { min: 450, max: 459, name: "Japan" },
    { min: 490, max: 499, name: "Japan" },
    { min: 500, max: 509, name: "UK" },
    { min: 640, max: 649, name: "Finland" },
    { min: 690, max: 699, name: "China" },
    { min: 730, max: 739, name: "Sweden" },
    { min: 760, max: 769, name: "Switzerland" },
    { min: 870, max: 879, name: "Netherlands" },
    { min: 900, max: 919, name: "Austria" }
];
# Contribute a translation

Copy `en.tsv` to a new file in this folder. Name it with a lowercase language tag, such as `fr.tsv`, `pt-br.tsv`, or `sr-latn.tsv`. A base language file like `fr.tsv` also covers regional variants such as French (Canada) unless a more specific file exists.

Each line has two quoted fields separated by a tab.

```text
"Save"	"Enregistrer"
"Pause and restore"	"Mettre en pause et restaurer"
```

- Keep the first field exactly as written in `en.tsv`. Translate the second field.
- Save as UTF-8. Escape quotation marks as `\"` and backslashes as `\\`. Preserve spaces inside the quotes when they are used to join a message with a number or error detail.
- You can leave the second field empty or omit a row. Missing messages fall back to English.
- Submit the new language file in a pull request. The build picks it up automatically for Windows, the Ubuntu settings app, and the GNOME extension.

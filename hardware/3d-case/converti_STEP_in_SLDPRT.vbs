' Solar Heating Controller — importazione STEP -> SolidWorks SLDPRT
' Richiede Windows con SolidWorks installato. Eseguire accanto ai file STEP.
' Le superfici restano solide; la cronologia feature non viene ricostruita.

Option Explicit
Dim fso, baseFolder, swApp, parts, index, stepPath, partPath
Dim swModel, importErrors, importWarnings, saveErrors, saveWarnings, success

Set fso = CreateObject("Scripting.FileSystemObject")
baseFolder = fso.GetParentFolderName(WScript.ScriptFullName)
parts = Array("SolarHeatingCase_BASE", "SolarHeatingCase_LID")

On Error Resume Next
Set swApp = CreateObject("SldWorks.Application")
If Err.Number <> 0 Then
    WScript.Echo "Impossibile avviare SolidWorks: " & Err.Description
    WScript.Quit 1
End If
On Error GoTo 0
swApp.Visible = True

For index = 0 To UBound(parts)
    stepPath = fso.BuildPath(baseFolder, parts(index) & ".step")
    partPath = fso.BuildPath(baseFolder, parts(index) & ".SLDPRT")

    If Not fso.FileExists(stepPath) Then
        WScript.Echo "STEP non trovato: " & stepPath
        WScript.Quit 2
    End If

    importErrors = 0
    importWarnings = 0
    Set swModel = swApp.OpenDoc6(stepPath, 1, 1, "", importErrors, importWarnings)
    If swModel Is Nothing Then
        WScript.Echo "Importazione fallita: " & stepPath
        WScript.Echo "Provare File > Apri > STEP in SolidWorks."
        WScript.Quit 3
    End If

    saveErrors = 0
    saveWarnings = 0
    success = swModel.Extension.SaveAs(partPath, 0, 1, Nothing, saveErrors, saveWarnings)
    If Not success Then
        WScript.Echo "Salvataggio SLDPRT non riuscito: " & saveErrors
        WScript.Quit 4
    End If

    WScript.Echo "Creato: " & partPath
    swApp.CloseDoc swModel.GetTitle()
Next

WScript.Echo "Conversione conclusa."

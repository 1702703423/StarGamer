param(
    [string]$InPath,
    [string]$OutPath
)
$ErrorActionPreference = "Stop"
$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
try {
    $doc = $word.Documents.Open($InPath, $false, $true)
    $doc.Fields.Update() | Out-Null
    foreach ($toc in $doc.TablesOfContents) { $toc.Update() | Out-Null }
    $doc.ExportAsFixedFormat($OutPath, 17)
    $doc.Close($false)
    Write-Output "PDF exported OK"
} finally {
    $word.Quit()
}

param([Parameter(Mandatory)][string]$Package, [Parameter(Mandatory)][string]$Project)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$projectDir = (Resolve-Path $Project).Path
$name = (Get-Content (Join-Path $projectDir 'game.json') -Raw | ConvertFrom-Json).name
$condition = [System.Windows.Automation.PropertyCondition]::new(
    [System.Windows.Automation.AutomationElement]::NameProperty, $name)
$exe = Join-Path (Resolve-Path $Package).Path 'AiverseStudio.exe'
$process = Start-Process -FilePath $exe -ArgumentList "--project `"$projectDir`"" -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "Studio exited with code $($process.ExitCode)" }
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            $root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
            $projectLabel = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
            if ($projectLabel) {
                Write-Output "Packaged Studio displayed project '$name'."
                return
            }
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Studio did not display '$name' in its native window within 90 seconds."
} finally {
    if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
}

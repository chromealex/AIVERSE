param([Parameter(Mandatory)][string]$Package, [Parameter(Mandatory)][string]$Project)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$projectDir = (Resolve-Path $Project).Path
$name = (Get-Content (Join-Path $projectDir 'game.json') -Raw | ConvertFrom-Json).name
$condition = [System.Windows.Automation.PropertyCondition]::new(
    [System.Windows.Automation.AutomationElement]::NameProperty, $name)
function Find-NamedControl($Parent, [string]$Label) {
    $match = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::NameProperty, $Label)
    return $Parent.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $match)
}
function Wait-NamedControl($Parent, [string]$Label) {
    $until = [DateTime]::UtcNow.AddSeconds(12)
    do {
        $element = Find-NamedControl $Parent $Label
        if ($element -and !$element.Current.IsOffscreen) { return $element }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $until)
    throw "Studio did not expose '$Label' in its visible controls."
}
function Invoke-Control($Element) {
    $Element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
}
function Test-SceneDock($Root) {
    $skip = Find-NamedControl $Root 'Skip'
    if ($skip -and !$skip.Current.IsOffscreen) { Invoke-Control $skip }
    $dock = Wait-NamedControl $Root 'Dock dock-center'
    $width = $dock.Current.BoundingRectangle.Width
    foreach ($action in 'Maximize','Restore') {
        Invoke-Control (Wait-NamedControl $dock 'Dock area menu')
        Invoke-Control (Wait-NamedControl $Root $action)
        $until = [DateTime]::UtcNow.AddSeconds(12)
        do {
            $dock = Wait-NamedControl $Root 'Dock dock-center'
            $current = $dock.Current.BoundingRectangle.Width
            $matched = if ($action -eq 'Maximize') { $current -gt $width + 20 } else { [Math]::Abs($current - $width) -le 2 }
            if ($matched) { break }
            Start-Sleep -Milliseconds 150
        } while ([DateTime]::UtcNow -lt $until)
        if (!$matched) { throw "$action left Scene at width $current (initial width $width)." }
    }
    Write-Output "Scene dock expanded and returned to its original width."
}
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
                Test-SceneDock $root
                return
            }
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Studio did not display '$name' in its native window within 90 seconds."
} finally {
    if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
}

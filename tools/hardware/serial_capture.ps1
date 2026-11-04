param(
    [string]$Port = 'COM5',
    [string[]]$Commands = @(),
    [int]$CaptureSeconds = 10,
    [int]$StartDelaySeconds = 0,
    [string]$StimulusWav = '',
    [string]$LogPath,
    [string]$StopPattern = '',
    [switch]$Quiet
)
$ErrorActionPreference = 'Stop'
$taskPort = [IO.Ports.SerialPort]::new($Port,1000000,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$taskPort.RtsEnable = $true
$taskPort.DtrEnable = $false
$taskPort.Encoding = [Text.Encoding]::UTF8
$taskPort.ReadBufferSize = 1048576
$taskPort.WriteTimeout = 3000
$taskText = [Text.StringBuilder]::new()
$taskPlayer = $null
if ($StimulusWav) {
    $taskWavPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($StimulusWav)
    $taskPlayer = [System.Media.SoundPlayer]::new($taskWavPath)
    $taskPlayer.Load()
}
$taskPort.Open()
try {
    Start-Sleep -Milliseconds 200
    [void]$taskText.Append($taskPort.ReadExisting())
    if ($StartDelaySeconds -gt 0) { Start-Sleep -Seconds $StartDelaySeconds }
    foreach ($taskCommand in $Commands) {
        $taskPort.Write($taskCommand + "`r`n")
        Start-Sleep -Milliseconds 150
        [void]$taskText.Append($taskPort.ReadExisting())
    }
    if ($taskPlayer) { $taskPlayer.Play() }
    $taskTimer = [Diagnostics.Stopwatch]::StartNew()
    while ($taskTimer.Elapsed.TotalSeconds -lt $CaptureSeconds) {
        $taskPart = $taskPort.ReadExisting()
        if ($taskPart) {
            [void]$taskText.Append($taskPart)
            if ($StopPattern -and $taskText.ToString().Contains($StopPattern)) { break }
        }
        Start-Sleep -Milliseconds 10
    }
    [void]$taskText.Append($taskPort.ReadExisting())
} finally {
    if ($taskPlayer) { $taskPlayer.Stop(); $taskPlayer.Dispose() }
    $taskPort.Close(); $taskPort.Dispose()
}
if ($LogPath) {
    $taskAbsoluteLog = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($LogPath)
    [IO.File]::WriteAllText($taskAbsoluteLog,$taskText.ToString(),[Text.UTF8Encoding]::new($false))
}
if ($Quiet) { Write-Output "Captured $($taskText.Length) characters to $LogPath" }
else { Write-Output $taskText.ToString() }

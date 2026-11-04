param([string]$Port='COM5',[Parameter(Mandatory)][string]$LogPath,[int]$Seconds=60)
$ErrorActionPreference='Stop'
$taskPort=[IO.Ports.SerialPort]::new($Port,1000000,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$taskPort.RtsEnable=$true;$taskPort.DtrEnable=$false
$taskPort.Encoding=[Text.Encoding]::UTF8;$taskPort.ReadBufferSize=1048576
$taskLog=[Text.StringBuilder]::new()
$taskComplete=$false
try {
    $taskPort.Open()
    $taskPort.Write("focusmate_ctl add`r`n")
    $taskReady=[Diagnostics.Stopwatch]::StartNew()
    while($taskReady.Elapsed.TotalSeconds -lt 12) {
        [void]$taskLog.Append($taskPort.ReadExisting())
        if($taskLog.ToString().Contains('[pixel] page=add')){break}
        Start-Sleep -Milliseconds 15
    }
    if(!$taskLog.ToString().Contains('[pixel] page=add')){throw 'UI did not reach add page.'}
    Write-Output 'READY: physical KEY1/KEY2 monitor is active.'
    $taskClock=[Diagnostics.Stopwatch]::StartNew()
    while($taskClock.Elapsed.TotalSeconds -lt $Seconds) {
        [void]$taskLog.Append($taskPort.ReadExisting())
        $taskText=$taskLog.ToString()
        if(([regex]::Matches($taskText,'\[pixel-key\] pressed=0x2 ').Count -ge 2) -and
           $taskText.Contains('[pixel-key] pressed=0x1 ') -and
           $taskText.Contains('[pixel] page=standby')) {
            $taskComplete=$true;break
        }
        Start-Sleep -Milliseconds 15
    }
} finally {
    if($taskPort.IsOpen){$taskPort.Close()};$taskPort.Dispose()
    [IO.File]::WriteAllText($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($LogPath),$taskLog.ToString(),[Text.UTF8Encoding]::new($false))
}
Write-Output $taskLog.ToString()
Write-Output "KEY_SEQUENCE_COMPLETE=$taskComplete"

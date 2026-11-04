param([string]$Port='COM5',[string]$LogPath,[int]$Rounds=3)
$ErrorActionPreference='Stop'
$taskPort=[IO.Ports.SerialPort]::new($Port,1000000,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$taskPort.RtsEnable=$true;$taskPort.DtrEnable=$false
$taskPort.Encoding=[Text.Encoding]::UTF8;$taskPort.ReadBufferSize=1048576
$taskLog=[Text.StringBuilder]::new()
function Ui-Step([string]$action,[string]$expected) {
    $taskStart=$taskLog.Length
    $taskPort.Write("focusmate_ctl $action`r`n")
    $taskClock=[Diagnostics.Stopwatch]::StartNew()
    while($taskClock.Elapsed.TotalSeconds -lt 12) {
        [void]$taskLog.Append($taskPort.ReadExisting())
        if($taskLog.ToString($taskStart,$taskLog.Length-$taskStart).Contains($expected)){return}
        Start-Sleep -Milliseconds 10
    }
    throw "UI did not reach $expected after $action"
}
$taskPort.Open()
try {
    Ui-Step 'home' '[pixel] page=standby'
    Ui-Step 'status' '[pixel-status] page=standby'
    $taskBaseline=[regex]::Match($taskLog.ToString(),'\[pixel-status\] page=standby tasks=(\d+)').Groups[1].Value
    $taskPort.Write("free`r`n");Start-Sleep -Milliseconds 300
    [void]$taskLog.Append($taskPort.ReadExisting())
    for($taskRound=0;$taskRound -lt $Rounds;$taskRound++) {
        Ui-Step 'add' '[pixel] page=add'
        Ui-Step 'talk' '[pixel] page=recording'
        Start-Sleep -Milliseconds 350
        Ui-Step 'cancel-record' '[pixel] page=add'
        Ui-Step 'audio-status' '[pcm-client] active=0 playing=0 ready=0 signal=0 bytes=0'
        Ui-Step 'talk' '[pixel] page=recording'
        Start-Sleep -Milliseconds 350
        Ui-Step 'home' '[pixel] page=standby'
        Ui-Step 'audio-status' '[pcm-client] active=0 playing=0'
        Ui-Step 'status' "[pixel-status] page=standby tasks=$taskBaseline"
        Write-Output "Completed cancel/Home cycle $($taskRound+1)/$Rounds"
    }
    $taskPort.Write("ps`r`nfree`r`n")
    Start-Sleep -Milliseconds 800
    [void]$taskLog.Append($taskPort.ReadExisting())
} finally {
    $taskPort.Write("focusmate_ctl home`r`n")
    $taskPort.Close();$taskPort.Dispose()
    [IO.File]::WriteAllText($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($LogPath),$taskLog.ToString(),[Text.UTF8Encoding]::new($false))
}
Write-Output $taskLog.ToString()
Write-Output "PASS: $Rounds cancel/Home cycles; no synthetic tasks created."

param([string]$Port='COM5',[Parameter(Mandatory)][string]$StimulusWav,[Parameter(Mandatory)][string]$LogPath)
$ErrorActionPreference='Stop'
$taskPlayer=[System.Media.SoundPlayer]::new($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($StimulusWav))
$taskPlayer.Load()
$taskPort=[IO.Ports.SerialPort]::new($Port,1000000,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$taskPort.RtsEnable=$true
$taskPort.DtrEnable=$false
$taskPort.Encoding=[Text.Encoding]::UTF8
$taskPort.ReadBufferSize=1048576
$taskLog=[Text.StringBuilder]::new()
$taskStopped=$false
function Wait-UiText([string]$needle,[int]$start,[int]$timeout=12) {
    $taskWait=[Diagnostics.Stopwatch]::StartNew()
    while($taskWait.Elapsed.TotalSeconds -lt $timeout) {
        [void]$taskLog.Append($taskPort.ReadExisting())
        if($taskLog.ToString($start,$taskLog.Length-$start).Contains($needle)){return}
        Start-Sleep -Milliseconds 10
    }
    throw "Timed out waiting for $needle"
}
$taskPort.Open()
try {
    foreach($taskPair in @(@('enter','page=welcome'),@('add','page=add'),@('talk','page=recording'))) {
        $taskAction=$taskPair[0]
        $taskStart=$taskLog.Length
        $taskPort.Write("focusmate_ctl $taskAction`r`n")
        Wait-UiText $taskPair[1] $taskStart
    }
    Start-Sleep -Milliseconds 350
    $taskPlayer.Play()
    $taskTimer=[Diagnostics.Stopwatch]::StartNew()
    $taskFlags=@{}
    $taskStopOffset=0
    while($taskTimer.Elapsed.TotalSeconds -lt 5.1) {
        [void]$taskLog.Append($taskPort.ReadExisting())
        $taskNow=$taskTimer.Elapsed.TotalSeconds
        if($taskNow -ge 1.0 -and !$taskFlags.ContainsKey('meter1')) {
            $taskFlags['meter1']=$true
            $taskPort.Write("focusmate_ctl audio-status`r`n")
        }
        if($taskNow -ge 2.5 -and !$taskFlags.ContainsKey('meter2')) {
            $taskFlags['meter2']=$true
            $taskPort.Write("focusmate_ctl audio-status`r`n")
        }
        if($taskNow -ge 5.0 -and !$taskStopped) {
            $taskStopOffset=$taskLog.Length
            $taskPort.Write("focusmate_ctl talk`r`n")
            $taskStopped=$true
        }
        Start-Sleep -Milliseconds 10
    }
    Wait-UiText 'page=voice-ready' $taskStopOffset
    $taskStatusOffset=$taskLog.Length
    $taskPort.Write("focusmate_ctl audio-status`r`nfocusmate_ctl status`r`n")
    Wait-UiText '[pixel-status] page=voice-ready' $taskStatusOffset
    [void]$taskLog.Append($taskPort.ReadExisting())
} finally {
    if(!$taskStopped) {
        $taskPort.Write("focusmate_ctl home`r`n")
        Start-Sleep -Milliseconds 300
        [void]$taskLog.Append($taskPort.ReadExisting())
    }
    $taskPlayer.Stop();$taskPlayer.Dispose()
    $taskPort.Close();$taskPort.Dispose()
    [IO.File]::WriteAllText($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($LogPath),$taskLog.ToString(),[Text.UTF8Encoding]::new($false))
}
$taskText=$taskLog.ToString()
Write-Output $taskText
if($taskText -notmatch 'page=recording' -or $taskText -notmatch 'page=voice-ready') {
    throw 'UI did not complete the real-recording to voice-ready path; inspect the saved log.'
}
if($taskText -notmatch 'active=0 playing=0 ready=1 signal=1 .*error=0') {
    throw 'Final PCM status was not a successful stopped recording.'
}
Write-Output 'PASS: real PCM capture, signal meter and manual UI endpoint.'

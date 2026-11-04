param(
  [string]$Port='COM5',
  [string]$ConfigPath=(Join-Path $env:TEMP 'focusmate-usb-cloud.json'),
  [string]$LogPath='output/firmware/2026-10-24/serial-agent-test.log',
  [string[]]$Prompts=@('请只回复：USB链路已打通，校验码482731。','上一条用户消息里的校验码是什么？只回复数字。'),
  [int]$TimeoutSeconds=150,
  [switch]$AlreadyRunning
)
$ErrorActionPreference='Stop'
$cfg=Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
if(-not $cfg.api_key -or $cfg.llm_host -ne 'api.deepseek.com' -or $cfg.llm_path -ne '/v1/chat/completions'){throw 'Missing/unsupported cloud configuration'}
$logFull=[IO.Path]::GetFullPath($LogPath)
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($logFull,'',$utf8)
function Record([string]$Text){
  $safe=$Text.Replace([string]$cfg.api_key,'[REDACTED]')
  [IO.File]::AppendAllText($logFull,$safe+"`n",$utf8)
  Write-Output $safe
}
$p=[IO.Ports.SerialPort]::new($Port,1000000,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$p.Encoding=$utf8;$p.RtsEnable=$true;$p.DtrEnable=$false;$p.WriteTimeout=5000
$client=[Net.Http.HttpClient]::new()
$client.Timeout=[TimeSpan]::FromSeconds(30)
function Send-Frame($Frame){
  $bytes=$utf8.GetBytes(($Frame|ConvertTo-Json -Compress -Depth 40)+"`n")
  for($offset=0;$offset -lt $bytes.Length;$offset+=64){
    $p.Write($bytes,$offset,[Math]::Min(64,$bytes.Length-$offset))
    Start-Sleep -Milliseconds 8
  }
}
$p.Open()
try{
  $pending=[Text.StringBuilder]::new();$index=0;$waiting=$false;$requests=0;$replies=0
  if($AlreadyRunning){Send-Frame @{op='ask';text=$Prompts[$index]};$waiting=$true;$index++}
  else{$p.Write("`r`nserial_agent`r`n")}
  $deadline=(Get-Date).AddSeconds($TimeoutSeconds)
  while((Get-Date)-lt $deadline){
    $chunk=$p.ReadExisting()
    if(-not $chunk){Start-Sleep -Milliseconds 20;continue}
    [void]$pending.Append($chunk)
    while($true){
      $text=$pending.ToString();$newline=$text.IndexOf("`n")
      if($newline -lt 0){break}
      $line=$text.Substring(0,$newline).TrimEnd("`r")
      [void]$pending.Remove(0,$newline+1)
      $marker=$line.IndexOf('@FM@')
      if($marker -lt 0){if($line){Record $line};continue}
      try{$frame=$line.Substring($marker+4)|ConvertFrom-Json}catch{Record 'ERROR: malformed serial JSON';throw}
      switch($frame.op){
        'ready'{Record ('DEVICE_READY '+$frame.text);if(-not $waiting){Send-Frame @{op='ask';text=$Prompts[$index]};Record ('USER '+$Prompts[$index]);$waiting=$true;$index++}}
        'accepted'{Record 'DEVICE_ACCEPTED'}
        'http'{
          $requests++
          if($frame.host -ne $cfg.llm_host -or $frame.path -ne $cfg.llm_path){throw 'Device requested non-allowlisted endpoint'}
          $request=[Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::Post,('https://'+$cfg.llm_host+$cfg.llm_path))
          $request.Headers.Authorization=[Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer',[string]$cfg.api_key)
          $request.Content=[Net.Http.StringContent]::new([string]$frame.body,$utf8,'application/json')
          $response=$null
          try{
            $response=$client.SendAsync($request).GetAwaiter().GetResult()
            $body=$response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            $status=[int]$response.StatusCode
            Record ('CLOUD_HTTP id='+$frame.id+' status='+$status+' request_bytes='+$utf8.GetByteCount([string]$frame.body)+' response_bytes='+$utf8.GetByteCount($body))
            if($utf8.GetByteCount($body) -ge [int]$frame.response_cap){$status=-5;$body=''}
            Send-Frame @{op='http_response';id=$frame.id;status=$status;body=$body}
          }catch{Record ('CLOUD_ERROR '+$_.Exception.GetType().Name);Send-Frame @{op='http_response';id=$frame.id;status=-1;body=''}}
          finally{if($response){$response.Dispose()};$request.Dispose()}
        }
        'reply'{
          Record ('DEVICE_REPLY '+$frame.text)
          # Ignore upstream progress text; only a completed reply advances a turn.
          if($frame.text -match '^(正在思考中|稍等，处理中|让我查一下)\.\.\.$'){continue}
          $replies++;$waiting=$false
          if($index -lt $Prompts.Count){Send-Frame @{op='ask';text=$Prompts[$index]};Record ('USER '+$Prompts[$index]);$index++;$waiting=$true}
          else{Record ('RESULT requests='+$requests+' replies='+$replies);return}
        }
        'error'{Record ('DEVICE_ERROR '+$frame.text);throw 'Device reported error'}
      }
    }
  }
  throw ('Serial Agent test timed out. requests='+$requests+' replies='+$replies)
}finally{$p.Close();$p.Dispose();$client.Dispose();$cfg=$null}

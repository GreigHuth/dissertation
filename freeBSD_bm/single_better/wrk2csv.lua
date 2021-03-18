--NOT MY CODE https://gist.github.com/YutaroHayakawa/7f4a1447bc7d66bb42cd529dfe93a329

done = function(summary, latency, requests)
  -- open output file
  f = io.open("INM0_ring1024_50KB_T0.csv", "a+")
  
  
  f:write(string.format("%f,%f,%f,%f,%f,%f,%f,%f,%d,%d,%d\n",
  latency.min, latency.max, latency.mean, latency.stdev, latency:percentile(50), latency:percentile(75), latency:percentile(90), latency:percentile(99), summary["duration"], summary["requests"], summary["bytes"]))
  
  f:close()
end

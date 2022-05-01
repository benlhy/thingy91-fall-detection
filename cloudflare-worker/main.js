addEventListener("fetch", event => {
    event.respondWith(handleRequest(event.request,event))
  })
  
  async function handleRequest(request,event) {
  
    const { headers } = request
    const contentType = headers.get("content-type") || ""
    if (request.method == "POST") {
      console.log("Post Request received")
      var data = ""
      if (contentType.includes("application/json")) {
        // get the full request
        data = await request.json()
        var data_str = JSON.stringify(data)
        console.log(`Captured ID is ${data["id"]}`)
  
        // send out the request to our backend
        const init = {
          method: "GET",
        };
        console.log("Sending out GET to another server")
        
       
        var url = `[YOUR GCP URL HERE]/alerts/${data["id"]}`
        console.log(`Url is: ${url}`)
        
        const response = await fetch(url, init);
        // await the response
        event.waitUntil(console.log(response))
  
        
        return new Response(`POST alert: ${data["id"]} - okay`);
        
      }
    } else {
  
      return new Response("NA");
    }
  }
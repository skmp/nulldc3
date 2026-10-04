// Stands in for the devtools application: opens the debugger socket the way the real client does.
const socket = new WebSocket(`${location.protocol === "https:" ? "wss:" : "ws:"}//${location.host}/ws`);
socket.addEventListener("open", () => socket.send("hello from the test bundle"));
socket.addEventListener("message", (event) => {
  document.getElementById("root").textContent = `echo: ${event.data}`;
});

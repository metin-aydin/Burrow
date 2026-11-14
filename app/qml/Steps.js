.pragma library

// Step lists for the progress UI. qsTr isn't available in a .pragma library, so labels are passed in.
function tunnelSteps(type, tr) {
    if (type === "tailscale")
        return [{ id: "start", label: tr.tsStart }, { id: "login", label: tr.tsLogin }]
    if (type === "cloudflare")
        return [{ id: "app", label: tr.cfApp }, { id: "login", label: tr.cfLogin }]
    return [{ id: "connect", label: tr.gpConnect }, { id: "portal", label: tr.gpPortal },
            { id: "gateway", label: tr.gpGateway }, { id: "tunnel", label: tr.gpTunnel }]
}

function rdpSteps(tr, name) {
    return [{ id: "connecting", label: tr.rdpConnect.arg(name) }, { id: "certificate", label: tr.rdpCert },
            { id: "signing_in", label: tr.rdpSignIn }, { id: "opening", label: tr.rdpOpen }]
}

// Maps a tunnel status map to the step list's current id.
function tunnelCurrent(st) {
    if (!st || !st.state) return ""
    if (st.state === "connected") return "done"
    if (st.step === "otp_wait" || st.step === "otp_manual") return "gateway"
    return st.step || ""
}

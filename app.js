const $ = (id) => document.getElementById(id);
const form = $("searchForm");
const button = $("submitBtn");
const message = $("message");
const reportSection = $("report");

document.querySelectorAll(".nav").forEach((nav) => {
  nav.addEventListener("click", () => {
    document.querySelectorAll(".nav").forEach((n) => n.classList.remove("active"));
    nav.classList.add("active");
    $("analyzeView").classList.toggle("hidden", nav.dataset.view !== "analyze");
    $("historyView").classList.toggle("hidden", nav.dataset.view !== "history");
    $("passwordView").classList.toggle("hidden", nav.dataset.view !== "password");
    if (nav.dataset.view === "history") loadHistory();
  });
});

function showMessage(text, error = false) {
  message.textContent = text;
  message.classList.remove("hidden", "error");
  if (error) message.classList.add("error");
}
function escapeHtml(value) {
  return String(value ?? "").replace(/[&<>"']/g, (c) => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
}
function safeUrl(value) {
  try {
    const url = new URL(value);
    return ["http:", "https:"].includes(url.protocol) ? url.href : "#";
  } catch {
    return "#";
  }
}
function renderReport(data) {
  reportSection.classList.remove("hidden");
  $("reportId").textContent = "ASSESSMENT #" + data.id;
  $("score").textContent = data.score;
  $("meterFill").style.width = data.score + "%";
  $("level").textContent = data.level;
  $("level").className = "level-badge " + data.level.toLowerCase();
  $("termLabel").textContent = data.term;
  $("resultCount").textContent = data.results.length;
  $("resultCountLabel").textContent = data.results.length + " results";
  $("indicators").innerHTML = data.indicators.length
    ? data.indicators.map((item) => `<div class="indicator">${escapeHtml(item)}</div>`).join("")
    : `<div class="empty">No indicators detected.</div>`;
  const recommendations = data.score >= 70
    ? ["Review each result manually before drawing conclusions.", "Remove unnecessary personal details from public profiles.", "Review privacy settings on relevant accounts."]
    : data.score >= 35
      ? ["Review public profiles and remove information you do not need to share.", "Avoid reusing usernames for sensitive accounts.", "Review account privacy settings."]
      : ["Review privacy settings periodically.", "Use unique usernames where practical.", "Recheck your public footprint occasionally."];
  $("recommendations").innerHTML = recommendations.map((x) => `<li>${escapeHtml(x)}</li>`).join("");
  $("resultsList").innerHTML = data.results.length
    ? data.results.map((r) => `<div class="result-item"><a href="${escapeHtml(safeUrl(r.url))}" target="_blank" rel="noopener noreferrer">${escapeHtml(r.title || r.url)}</a><p>${escapeHtml(r.description || "")}</p></div>`).join("")
    : `<div class="empty">No public results were returned. This does not prove no information exists.</div>`;
  reportSection.scrollIntoView({ behavior: "smooth", block: "start" });
}
form.addEventListener("submit", async (event) => {
  event.preventDefault();
  const term = $("term").value.trim();
  if (!term) return;
  button.disabled = true;
  button.innerHTML = "Searching public web…";
  message.classList.add("hidden");
  reportSection.classList.add("hidden");
  try {
    const response = await fetch("/api/analyze", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ term }),
      signal: AbortSignal.timeout(25000)
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || "Analysis failed.");
    renderReport(data);
  } catch (error) {
    showMessage(error.name === "TimeoutError" || error.name === "AbortError" ? "The request timed out. The external service may be slow or temporarily unavailable. Please try again." : error.message + " Make sure the C++ server is running and your internet connection works.", true);
  } finally {
    button.disabled = false;
    button.innerHTML = 'Analyze footprint <span>↗</span>';
  }
});
async function loadHistory() {
  const target = $("historyTable");
  target.innerHTML = `<div class="empty">Loading history…</div>`;
  try {
    const response = await fetch("/api/history");
    const rows = await response.json();
    if (!rows.length) {
      target.innerHTML = `<div class="empty">No assessments saved yet. Run your first analysis.</div>`;
      return;
    }
    target.innerHTML = `<table class="history-table"><thead><tr><th>ID</th><th>SEARCH TERM</th><th>SCORE</th><th>LEVEL</th><th>DATE</th><th>RESULTS</th></tr></thead><tbody>${rows.map((r) => `<tr><td>#${r.id}</td><td>${escapeHtml(r.term)}</td><td>${r.score}/100</td><td><span class="mini-level ${escapeHtml(r.level)}">${escapeHtml(r.level)}</span></td><td>${escapeHtml(r.date)}</td><td>${r.count}</td></tr>`).join("")}</tbody></table>`;
  } catch {
    target.innerHTML = `<div class="empty">Could not load history. Check that the C++ server is running.</div>`;
  }
}
$("refreshHistory").addEventListener("click", loadHistory);

function assessPasswordStrength(password, breachCount) {
  const checks = [
    { label: `Length: ${password.length} characters`, passed: password.length >= 14 },
    { label: "Contains lowercase letters", passed: /[a-z]/.test(password) },
    { label: "Contains uppercase letters", passed: /[A-Z]/.test(password) },
    { label: "Contains numbers", passed: /[0-9]/.test(password) },
    { label: "Contains symbols", passed: /[^A-Za-z0-9]/.test(password) },
    { label: "Avoids repeated or sequential patterns", passed: !/(.)\1{2,}|1234|abcd|qwerty|password|letmein|admin/i.test(password) }
  ];
  let strengthPoints = checks.filter((item) => item.passed).length;
  let strength = strengthPoints <= 2 ? "WEAK" : strengthPoints <= 4 ? "MODERATE" : "STRONG";
  let score = Math.max(0, 100 - strengthPoints * 12);
  if (password.length < 8) score += 20;
  if (password.length < 12) score += 10;
  if (/(.)\1{2,}|1234|abcd|qwerty|password|letmein|admin/i.test(password)) score += 20;
  if (breachCount > 0) score += breachCount >= 1000 ? 60 : breachCount >= 10 ? 45 : 35;
  score = Math.min(100, score);
  if (breachCount > 0) strength = "COMPROMISED";
  const recommendations = [];
  if (password.length < 14) recommendations.push("Use a longer password or passphrase of at least 14 characters.");
  if (!/[a-z]/.test(password) || !/[A-Z]/.test(password) || !/[0-9]/.test(password) || !/[^A-Za-z0-9]/.test(password)) recommendations.push("Use a mix of uppercase and lowercase letters, numbers, and symbols, or use a long random passphrase.");
  if (/(.)\1{2,}|1234|abcd|qwerty|password|letmein|admin/i.test(password)) recommendations.push("Avoid common words, keyboard patterns, repeated characters, and predictable sequences.");
  if (breachCount > 0) recommendations.push("Change this password immediately anywhere it is used. Never reuse a compromised password.");
  if (breachCount === 0) recommendations.push("No match was found in the checked corpus; this does not prove the password has never leaked.");
  recommendations.push("Use a unique password for every account and consider a password manager.");
  return { checks, strength, score, recommendations };
}

const passwordForm = $("passwordForm");
passwordForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const passwordInput = $("passwordInput");
  const passwordBtn = $("passwordBtn");
  const passwordMessage = $("passwordMessage");
  const passwordResult = $("passwordResult");
  const password = passwordInput.value;
  if (!password) return;
  passwordBtn.disabled = true;
  passwordBtn.textContent = "Checking…";
  passwordMessage.classList.add("hidden");
  passwordResult.classList.add("hidden");
  try {
    const response = await fetch("/api/password-check", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ password }), signal: AbortSignal.timeout(25000) });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || "Password check failed.");
    const assessment = assessPasswordStrength(password, Number(data.count) || 0);
    $("passwordRiskScore").textContent = assessment.score;
    $("passwordRiskMeter").style.width = `${assessment.score}%`;
    $("passwordStrength").textContent = assessment.strength;
    $("passwordStrength").className = `level-badge ${assessment.strength === "WEAK" || assessment.strength === "COMPROMISED" ? "high" : assessment.strength === "MODERATE" ? "medium" : ""}`;
    $("passwordStrengthText").textContent = `${assessment.checks.filter((item) => item.passed).length} of ${assessment.checks.length} strength checks passed`;
    $("passwordStatus").textContent = data.found ? "FOUND" : "NO MATCH";
    $("passwordStatus").className = `level-badge ${data.found ? "high" : ""}`;
    $("passwordCount").textContent = data.found ? `${Number(data.count).toLocaleString()} occurrences in the corpus` : "No match in the checked corpus";
    $("passwordHeading").textContent = data.found ? "This password appears in known breaches" : "No match found in the known corpus";
    $("passwordDescription").textContent = data.found ? `This password appears ${Number(data.count).toLocaleString()} times in the Pwned Passwords corpus. Treat it as compromised and change it anywhere it was reused.` : data.message;
    $("passwordPrivacy").textContent = data.privacy;
    $("passwordComplexity").innerHTML = assessment.checks.map((item) => `<div class="indicator ${item.passed ? "passed" : "failed"}">${item.label}: <strong>${item.passed ? "Pass" : "Improve"}</strong></div>`).join("");
    $("passwordRecommendations").innerHTML = assessment.recommendations.map((item) => `<li>${escapeHtml(item)}</li>`).join("");
    passwordResult.classList.remove("hidden");
    passwordInput.value = "";
  } catch (error) {
    passwordMessage.textContent = error.name === "TimeoutError" || error.name === "AbortError" ? "The request timed out. The breach-check service may be slow or temporarily unavailable. Please try again." : error.message;
    passwordMessage.classList.remove("hidden");
    passwordMessage.classList.add("error");
  } finally {
    passwordBtn.disabled = false;
    passwordBtn.innerHTML = 'Check password <span>↗</span>';
  }
});

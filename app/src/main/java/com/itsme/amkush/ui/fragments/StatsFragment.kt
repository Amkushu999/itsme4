package com.itsme.amkush.ui.fragments

  import android.os.Environment
  import androidx.compose.animation.core.*
  import androidx.compose.foundation.*
  import androidx.compose.foundation.layout.*
  import androidx.compose.foundation.shape.*
  import androidx.compose.material3.Text
  import androidx.compose.runtime.*
  import androidx.compose.ui.*
  import androidx.compose.ui.draw.*
  import androidx.compose.ui.graphics.*
  import androidx.compose.ui.text.font.*
  import androidx.compose.ui.text.style.*
  import androidx.compose.ui.unit.*
  import com.itsme.amkush.model.HookStatus
  import com.itsme.amkush.model.HookStatusRegistry
  import com.itsme.amkush.utils.SharedPrefs
  import kotlinx.coroutines.*
  import java.io.File
  import java.text.SimpleDateFormat
  import java.util.*

  private val Violet   = Color(0xFF6C63FF)
  private val GreenOk  = Color(0xFF4ADE80)
  private val TextSec  = Color(0x44FFFFFF)
  private val TextMid  = Color(0x88FFFFFF)
  private val Border   = Color(0x1AFFFFFF)
  private val Surface  = Color(0x12FFFFFF)

  // Terminal palette
  private val TermBg     = Color(0xFF07100A)
  private val TermBorder = Color(0xFF1C3A20)
  private val TermHeader = Color(0xFF0D1F10)
  private val CyanBright = Color(0xFF00E5FF)
  private val CyanDim    = Color(0xFF00B8D4)
  private val GreenLog   = Color(0xFF69FF47)
  private val YellowLog  = Color(0xFFFFD600)
  private val RedLog     = Color(0xFFFF4D6D)
  private val GreyLog    = Color(0xFF4A6050)

  @Composable
  fun StatsContent(
      targetPackage: String?,
      targetAppName: String?
  ) {
      val effName     = targetAppName ?: SharedPrefs.getTargetAppName()
      val hooks       = remember { HookStatusRegistry.getAllHooks() }
      val grouped     = remember(hooks) { hooks.groupBy { it.category } }
      val activeCount = remember(hooks) { hooks.count { it.isActive } }
      val totalCount  = hooks.size

      val pulse = rememberInfiniteTransition(label = "pulse")
      val blink by pulse.animateFloat(
          initialValue = 1f, targetValue = 0.3f,
          animationSpec = infiniteRepeatable(tween(1400), RepeatMode.Reverse),
          label = "blink"
      )

      var logLines by remember { mutableStateOf(emptyList<String>()) }
      LaunchedEffect(Unit) {
          while (isActive) {
              logLines = readLogFile()
              delay(1000L)
          }
      }

      Column(
          modifier = Modifier
              .fillMaxSize()
              .verticalScroll(rememberScrollState())
      ) {
          // Header
          Row(
              modifier = Modifier
                  .fillMaxWidth()
                  .padding(start = 20.dp, end = 20.dp, top = 16.dp, bottom = 8.dp),
              horizontalArrangement = Arrangement.SpaceBetween,
              verticalAlignment = Alignment.CenterVertically
          ) {
              Column {
                  Text("Hook Status", color = Color.White, fontSize = 16.sp, fontWeight = FontWeight.Bold)
                  Text("Active module diagnostics", color = TextSec, fontSize = 11.sp)
              }
              Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                  Box(Modifier.size(8.dp).clip(CircleShape).background(GreenOk.copy(alpha = blink)))
                  Text("ACTIVE", color = GreenOk, fontSize = 10.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
              }
          }

          // Target app pill
          if (!effName.isNullOrEmpty()) {
              Row(
                  modifier = Modifier
                      .fillMaxWidth()
                      .padding(horizontal = 16.dp)
                      .clip(RoundedCornerShape(12.dp))
                      .background(Surface)
                      .border(1.dp, Border, RoundedCornerShape(12.dp))
                      .padding(horizontal = 14.dp, vertical = 10.dp),
                  verticalAlignment = Alignment.CenterVertically,
                  horizontalArrangement = Arrangement.spacedBy(8.dp)
              ) {
                  Text("🎯", fontSize = 12.sp)
                  Text(
                      effName, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.SemiBold,
                      maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f)
                  )
              }
              Spacer(Modifier.height(10.dp))
          }

          // Summary card
          Box(
              modifier = Modifier
                  .fillMaxWidth()
                  .padding(horizontal = 16.dp)
                  .clip(RoundedCornerShape(16.dp))
                  .background(Color(0x1A4ADE80))
                  .border(1.dp, GreenOk.copy(0.25f), RoundedCornerShape(16.dp))
                  .padding(horizontal = 16.dp, vertical = 14.dp)
          ) {
              Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                  Column {
                      Text(
                          "$activeCount / $totalCount",
                          color = GreenOk, fontSize = 28.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace
                      )
                      Text("Hooks active", color = TextMid, fontSize = 12.sp)
                  }
                  Box(
                      modifier = Modifier.size(56.dp).clip(CircleShape)
                          .background(GreenOk.copy(0.15f)).border(2.dp, GreenOk.copy(0.4f), CircleShape),
                      contentAlignment = Alignment.Center
                  ) {
                      Text(
                          "${(activeCount * 100 / totalCount.coerceAtLeast(1))}%",
                          color = GreenOk, fontSize = 12.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace
                      )
                  }
              }
          }

          Spacer(Modifier.height(12.dp))

          // Per-category hook rows
          grouped.forEach { (category, hooksInCat) ->
              Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)) {
                  Text(category, color = Violet, fontSize = 11.sp, fontWeight = FontWeight.Bold,
                      letterSpacing = 2.sp, modifier = Modifier.padding(bottom = 8.dp, top = 8.dp))
                  Box(
                      modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp))
                          .background(Surface).border(1.dp, Border, RoundedCornerShape(16.dp))
                  ) {
                      Column {
                          hooksInCat.forEachIndexed { idx, hook ->
                              HookRow(hook = hook)
                              if (idx < hooksInCat.lastIndex)
                                  Box(Modifier.fillMaxWidth().height(1.dp).background(Color(0x0AFFFFFF)))
                          }
                      }
                  }
              }
          }

          Spacer(Modifier.height(16.dp))

          // Terminal log panel
          TerminalPanel(logLines = logLines)

          Spacer(Modifier.height(28.dp))
      }
  }

  @Composable
  private fun TerminalPanel(logLines: List<String>) {
      val scope      = rememberCoroutineScope()
      var saveMsg    by remember { mutableStateOf("") }
      val termScroll = rememberScrollState()

      LaunchedEffect(logLines.size) {
          if (logLines.isNotEmpty()) termScroll.scrollTo(termScroll.maxValue)
      }

      Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {

          // Title bar
          Row(
              modifier = Modifier
                  .fillMaxWidth()
                  .clip(RoundedCornerShape(topStart = 14.dp, topEnd = 14.dp))
                  .background(TermHeader)
                  .border(1.dp, TermBorder, RoundedCornerShape(topStart = 14.dp, topEnd = 14.dp))
                  .padding(horizontal = 14.dp, vertical = 9.dp),
              verticalAlignment = Alignment.CenterVertically,
              horizontalArrangement = Arrangement.SpaceBetween
          ) {
              Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                  Box(Modifier.size(9.dp).clip(CircleShape).background(Color(0xFFFF5F57)))
                  Box(Modifier.size(9.dp).clip(CircleShape).background(Color(0xFFFFBD2E)))
                  Box(Modifier.size(9.dp).clip(CircleShape).background(Color(0xFF28CA42)))
                  Spacer(Modifier.width(6.dp))
                  Text("facegate — live log", color = CyanDim, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                  Text(" (${logLines.size})", color = GreyLog, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
              }
              Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                  if (saveMsg.isNotEmpty())
                      Text(saveMsg, color = GreenLog, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                  Box(
                      modifier = Modifier
                          .clip(RoundedCornerShape(7.dp))
                          .background(Color(0xFF112211))
                          .border(1.dp, CyanDim.copy(0.35f), RoundedCornerShape(7.dp))
                          .clickable {
                              scope.launch {
                                  val ok = saveLog(logLines)
                                  saveMsg = if (ok) "✓ saved" else "✗ failed"
                                  delay(2500)
                                  saveMsg = ""
                              }
                          }
                          .padding(horizontal = 11.dp, vertical = 5.dp)
                  ) {
                      Text("⬇ Save", color = CyanBright, fontSize = 10.sp,
                          fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold)
                  }
              }
          }

          // Terminal body
          Box(
              modifier = Modifier
                  .fillMaxWidth()
                  .height(340.dp)
                  .clip(RoundedCornerShape(bottomStart = 14.dp, bottomEnd = 14.dp))
                  .background(TermBg)
                  .border(1.dp, TermBorder, RoundedCornerShape(bottomStart = 14.dp, bottomEnd = 14.dp))
          ) {
              if (logLines.isEmpty()) {
                  Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                      Text(
                          "$ waiting for facegate logs...

  Start injection to see live output",
                          color = GreyLog, fontSize = 11.sp, fontFamily = FontFamily.Monospace,
                          textAlign = TextAlign.Center
                      )
                  }
              } else {
                  Column(
                      modifier = Modifier.fillMaxSize()
                          .verticalScroll(termScroll)
                          .padding(horizontal = 10.dp, vertical = 8.dp),
                      verticalArrangement = Arrangement.spacedBy(2.dp)
                  ) {
                      logLines.forEach { line -> TermLine(line) }
                  }
              }
          }
      }
  }

  @Composable
  private fun TermLine(line: String) {
      val color = when {
          line.contains(" E/") || line.contains("[ERROR]") || line.contains("FATAL") -> RedLog
          line.contains(" W/") || line.contains("[WARN]")  || line.contains("WARN")  -> YellowLog
          line.contains(" I/") || line.contains("[INFO]")  || line.contains("INFO")  -> GreenLog
          else -> CyanBright
      }
      Text(
          text = line,
          color = color,
          fontSize = 10.5.sp,
          fontFamily = FontFamily.Monospace,
          lineHeight = 15.sp,
          overflow = TextOverflow.Ellipsis,
          maxLines = 3
      )
  }

  @Composable
  private fun HookRow(hook: HookStatus) {
      Row(
          modifier = Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 10.dp),
          verticalAlignment = Alignment.CenterVertically,
          horizontalArrangement = Arrangement.SpaceBetween
      ) {
          Text(hook.name, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Medium, modifier = Modifier.weight(1f))
          Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
              Box(Modifier.size(8.dp).clip(CircleShape).background(if (hook.isActive) Color(0xFF4ADE80) else Color(0xFFFF4D6D)))
              Text(
                  if (hook.isActive) "Active" else "Inactive",
                  color = if (hook.isActive) Color(0xFF4ADE80) else Color(0xFFFF4D6D),
                  fontSize = 11.sp, fontFamily = FontFamily.Monospace
              )
          }
      }
  }

  // ── I/O helpers ───────────────────────────────────────────────────────────────

  private fun readLogFile(): List<String> = try {
      val f = File(
          Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
          "com.itsme.amkush/amkush_hook_logs.txt"
      )
      if (!f.exists()) listOf("$ no log file yet — start injection to generate logs")
      else { val all = f.readLines(); if (all.size <= 300) all else all.takeLast(300) }
  } catch (e: Throwable) { listOf("$ cannot read log: ${e.message}") }

  private fun saveLog(lines: List<String>): Boolean = try {
      val dest = File(
          Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
          "facegate.txt"
      )
      val ts = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())
      dest.writeText(buildString {
          appendLine("# FaceGate Log Export — $ts")
          appendLine("# ${lines.size} lines")
          appendLine()
          lines.forEach { appendLine(it) }
      })
      true
  } catch (_: Throwable) { false }
  
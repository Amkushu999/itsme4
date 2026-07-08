package com.itsme.amkush.network

  import android.content.Context
  import com.google.gson.GsonBuilder
  import com.itsme.amkush.BuildConfig
  import com.itsme.amkush.security.LicenseGuard
  import okhttp3.OkHttpClient
  import okhttp3.logging.HttpLoggingInterceptor
  import retrofit2.Retrofit
  import retrofit2.converter.gson.GsonConverterFactory
  import java.util.concurrent.TimeUnit

  object ApiClient {
      // Server URL is XOR-obfuscated in native; never stored as plaintext in Kotlin.
      private val BASE_URL: String by lazy {
          try {
              val url = LicenseGuard.nativeGetBaseUrl()
              if (url.isNotEmpty()) url.trimEnd('/') + "/"
              else "https://standing-panther-214.convex.site/"
          } catch (_: Throwable) {
              "https://standing-panther-214.convex.site/"
          }
      }

      private const val TIMEOUT_SECONDS = 30L

      @Volatile private var retrofit: Retrofit? = null

      private fun getClient(context: Context? = null): Retrofit {
          retrofit?.let { return it }
          return synchronized(this) {
              retrofit?.let { return it }

              val loggingInterceptor = HttpLoggingInterceptor().apply {
                  level = if (BuildConfig.DEBUG) {
                      HttpLoggingInterceptor.Level.BODY
                  } else {
                      HttpLoggingInterceptor.Level.NONE
                  }
              }

              val okHttpClient = OkHttpClient.Builder()
                  .addInterceptor(loggingInterceptor)
                  .connectTimeout(TIMEOUT_SECONDS, TimeUnit.SECONDS)
                  .readTimeout(TIMEOUT_SECONDS, TimeUnit.SECONDS)
                  .writeTimeout(TIMEOUT_SECONDS, TimeUnit.SECONDS)
                  .retryOnConnectionFailure(true)
                  .build()

              val gson = GsonBuilder()
                  .setLenient()
                  .create()

              val instance = Retrofit.Builder()
                  .baseUrl(BASE_URL)
                  .client(okHttpClient)
                  .addConverterFactory(GsonConverterFactory.create(gson))
                  .build()

              retrofit = instance
              instance
          }
      }

      fun getApiService(context: Context? = null): ApiService {
          return getClient(context).create(ApiService::class.java)
      }

      fun getBaseUrl(): String = BASE_URL
  }

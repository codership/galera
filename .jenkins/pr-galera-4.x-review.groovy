//

pipeline {
  agent { label 'built-in' }
  stages {

    stage ('Smoke Test') {
      steps {
        script {
          def res = sh(script: "echo -e \"$ghprbPullLongDescription\" | grep '^MYSQL_BRANCH=' ||:",
                                returnStdout: true)
          if (!res.isEmpty()) {
            env.MYSQL_BRANCH = res.split('=')[1].trim()
          } else {
            env.MYSQL_BRANCH = env.DEFAULT_MYSQL_BRANCH
          }
          echo sh(script: 'env|sort', returnStdout: true)

          build job: 'pr-galera-4.x-smoke-test', wait: true,
                  parameters: [ string(name: 'GALERA_BRANCH', value: env.ghprbActualCommit )]
        }
      }
    }

    stage ('Build') {
      steps {
        script {
          def bintarJob = build job: 'pr-build-galera-4.x-mysql-8.0-v26', wait: true,
            parameters: [
              string(name: 'GALERA_BRANCH', value: env.ghprbActualCommit ),
              string(name: 'MYSQL_BRANCH', value: env.MYSQL_BRANCH)
              ]
          env.BINTAR_JOB = bintarJob.getNumber().toString()
        }
      }
    }

    stage ('Run Tests') {
      parallel {
        stage ('MTR') {
          steps {
            build job: 'pr-mtr-galera-4.x-mysql-8.0-v26', wait: true,
              parameters: [ string(name: 'BUILD_SELECTOR', value: env.BINTAR_JOB) ]
          }
        }
        stage ('GcTest') {
          steps {
            build job: 'pr-sssc-galera-4.x-mysql-8.0-v26', wait: true,
              parameters: [ string(name: 'BUILD_SELECTOR', value: env.BINTAR_JOB) ]
          }
        }
      } // parallel
    }
  }
}
